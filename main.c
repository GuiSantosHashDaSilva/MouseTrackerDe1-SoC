/*
 * =========================================================================
 * main_menu_test.c: VERSÃO FINAL COMPLETA - TUDO EM UM ARQUIVO adson 1/12 15:30
 * =========================================================================
 */

 #include "api.h"
 #include <stdio.h>
 #include <stdlib.h>
 #include <stdint.h>
 #include <unistd.h>  // Para usleep()
 #include <fcntl.h>
 #include <string.h>
 #include <signal.h>
 #include <errno.h>

 #include "mouse_utils.h" //cabeçalho com funções de entrada do mouse
 
 #define MAX_PATH_LEN 256 // tamanho máximo para o path e nome do arquivo do mouse
 #define C_TIMEOUT_LOOPS 50

 // Variável global para o descritor de arquivo do mouse para que o handler possa fechar.
 int mouse_fd_global = -1;
 
 /* ===================================================================
  * FUNÇÕES DE CARREGAMENTO DE BMP
  * =================================================================== */
 
 #pragma pack(push, 1)
 typedef struct {
     uint16_t type;
     uint32_t size;
     uint16_t reserved1;
     uint16_t reserved2;
     uint32_t offset;
 } BMPHeader;
 
 typedef struct {
     uint32_t size;
     int32_t  width;
     int32_t  height;
     uint16_t planes;
     uint16_t bits;
     uint32_t compression;
     uint32_t imagesize;
     int32_t  xresolution;
     int32_t  yresolution;
     uint32_t ncolours;
     uint32_t importantcolours;
 } BMPInfoHeader;
 #pragma pack(pop)
 
 uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b) {
     return (uint8_t)((299 * r + 587 * g + 114 * b) / 1000);
 }
 
 int load_bmp(const char *filename, uint8_t *image_data) {
     FILE *file;
     BMPHeader header;
     BMPInfoHeader infoHeader;
     int y;
     int x;
    
     file = fopen(filename, "rb");
     if (!file) {
         printf(" Erro ao abrir '%s'\n", filename);
         return -1;
     }
    
     fread(&header, sizeof(BMPHeader), 1, file);
     if (header.type != 0x4D42) {
         printf(" Arquivo nao e BMP valido (assinatura 0x%X)\n", header.type);
         fclose(file);
         return -1;
     }
    
     fread(&infoHeader, sizeof(BMPInfoHeader), 1, file);
    
     if (infoHeader.width != IMG_WIDTH || abs(infoHeader.height) != IMG_HEIGHT) {
         printf(" Dimensao incorreta: %dx%d (esperado %dx%d)\n",
                infoHeader.width, abs(infoHeader.height), IMG_WIDTH, IMG_HEIGHT);
         fclose(file);
         return -1;
     }
    
     fseek(file, header.offset, SEEK_SET);
    
     int row_size = ((infoHeader.width * infoHeader.bits + 31) / 32) * 4;
    
     uint8_t *row_data = (uint8_t*)malloc(row_size);
     if (!row_data) {
         printf("\nErro ao alocar memoria para a linha do BMP\n");
         fclose(file);
         return -1;
     }
    
     printf("Carregando");
     for (y = 0; y < IMG_HEIGHT; y++) {
         fread(row_data, 1, row_size, file);
        
         for (x = 0; x < IMG_WIDTH; x++) {
             uint8_t gray;
            
             if (infoHeader.bits == 32) {
                 uint8_t b = row_data[x * 4 + 0];
                 uint8_t g = row_data[x * 4 + 1];
                 uint8_t r = row_data[x * 4 + 2];
                 gray = rgb_to_gray(r, g, b);
             } else if (infoHeader.bits == 24) {
                 uint8_t b = row_data[x * 3 + 0];
                 uint8_t g = row_data[x * 3 + 1];
                 uint8_t r = row_data[x * 3 + 2];
                 gray = rgb_to_gray(r, g, b);
             } else if (infoHeader.bits == 8) {
                 gray = row_data[x];
             } else {
                 printf("\nFormato %d bits nao suportado\n", infoHeader.bits);
                 free(row_data);
                 fclose(file);
                 return -1;
             }
            
             int addr = (IMG_HEIGHT - 1 - y) * IMG_WIDTH + x;
             image_data[addr] = gray;
         }
        
         if (y % 60 == 0) {
             printf(".");
             fflush(stdout);
         }
     }
    
     printf(" OK!\n");
     free(row_data);
     fclose(file);
     return 0;
 }
 
 /* ===================================================================
  * FUNÇÕES AUXILIARES DE TESTE
  * =================================================================== */
 
 void gerar_padrao_teste(uint8_t *image_data) {
     int y;
     int x;
     printf("   [C] Gerando padrao de teste (gradiente 320x240)...\n");
     for (y = 0; y < IMG_HEIGHT; y++) {
         for (x = 0; x < IMG_WIDTH; x++) {
             uint8_t pixel_value = (uint8_t)((x * 255) / (IMG_WIDTH - 1));
             image_data[y * IMG_WIDTH + x] = pixel_value;
         }
     }
 }
 
 int enviar_imagem_para_fpga(uint8_t *image_data) {
     int total_pixels = IMG_WIDTH * IMG_HEIGHT;
     int errors = 0;
     int i;
 
     printf("   [C] Enviando %d pixels para o FPGA (testando ASM_Store)...\n", total_pixels);
    
     for (i = 0; i < total_pixels; i++) {
         int status = ASM_Store(i, image_data[i]);
         if (status != 0) {
             printf("\n   [C] ERRO: ASM_Store falhou no pixel %d (codigo %d)\n", i, status);
             errors++;
             if (errors > 10) {
                 printf("   [C] Muitos erros, abortando envio.\n");
                 return -1;
             }
         }
     }
    
     printf("   [C] Envio de pixels OK.\n");
     printf("   [C] Testando ASM_Refresh()...\n");
     ASM_Refresh();
     usleep(100000);
 
     if (errors > 0) {
         printf("   [C] ERRO: %d falhas de escrita de pixel.\n", errors);
         return -1;
     }
     return 0;
 }
 
 int executar_algoritmo(const char *nome_algoritmo, void (*funcao_algoritmo)(void)) {
     printf("   [C] Executando '%s' (assincrono)...\n", nome_algoritmo);
    
     funcao_algoritmo();
    
     printf("   [C] Hardware iniciado. Aguardando FLAG_DONE (polling)...\n");
 
     int timeout_c = 0;
     while (ASM_Get_Flag_Done() == 0) {
         usleep(100000);
         timeout_c++;
        
         if (timeout_c > C_TIMEOUT_LOOPS) {
             printf("\n   [C] ERRO FATAL: TIMEOUT DO ALGORITMO '%s'!\n", nome_algoritmo);
             return -1;
         }
     }
    
     printf("   [C] FLAG_DONE recebida para '%s'!\n", nome_algoritmo);
 
     if (ASM_Get_Flag_Error() != 0) {
         printf("   [C] ATENCAO: O FPGA reportou um ERRO (Flag_Error) durante '%s'!\n", nome_algoritmo);
         return -1;
     }
 
     ASM_Pulse_Enable();
    
     printf("   [C] '%s' executado com sucesso.\n", nome_algoritmo);
     return 0;
 }
 
 /* ===================================================================
  * FUNÇÕES DE LEITURA DA FPGA
  * =================================================================== */
 
 int read_fpga_window(uint8_t *buffer, int x, int y, int width, int height) {
     if (!buffer) {
         printf("ERRO: Buffer nulo em read_fpga_window\n");
         return -1;
     }
    
     if (x < 0 || y < 0 || width <= 0 || height <= 0) {
         printf("ERRO: Parametros invalidos em read_fpga_window\n");
         return -1;
     }
    
     if (x + width > IMG_WIDTH || y + height > IMG_HEIGHT) {
         printf("ERRO: Janela fora dos limites da imagem\n");
         return -1;
     }
    
     int buffer_index = 0;
     int errors = 0;
     int row;
     int col;
    
     printf("   [C] Lendo janela (%d,%d) com tamanho %dx%d da FPGA...\n",
            x, y, width, height);
     for (row = 0; row < height; row++) {
         for (col = 0; col < width; col++) {
             int addr = (y + row) * IMG_WIDTH + (x + col);
             int pixel_value = ASM_Load(addr);
            
             if (pixel_value < 0 || pixel_value > 255) {
                 printf("   [C] AVISO: Valor invalido no pixel [%d,%d]: %d\n",
                        x + col, y + row, pixel_value);
                 errors++;
                 pixel_value = 0;
             }
            
             buffer[buffer_index++] = (uint8_t)pixel_value;
         }
     }
    
     if (errors > 0) {
         printf("   [C] AVISO: %d erros durante leitura da janela\n", errors);
     } else {
         printf("   [C] Janela lida com sucesso (%d pixels)\n", buffer_index);
     }
    
     return (errors > 0) ? -1 : 0;
 }
 
 void print_matrix(const uint8_t *data, int width, int height) {
    int row;
    int col;

     if (!data) {
         printf("ERRO: Dados nulos em print_matrix\n");
         return;
     }
    
     printf("\n=== MATRIZ %dx%d ===\n", width, height);
    
     int max_rows = (height > 20) ? 20 : height;
     int max_cols = (width > 40) ? 40 : width;
    
     if (height > max_rows || width > max_cols) {
         printf("(Mostrando apenas %dx%d da matriz completa)\n\n", max_cols, max_rows);
     }
    
     printf("     ");
     for (col = 0; col < max_cols; col++) {
         printf("%3d ", col);
     }
     if (width > max_cols) printf("...");
     printf("\n");
    
     printf("     ");
     for (col = 0; col < max_cols; col++) {
         printf("----");
     }
     if (width > max_cols) printf("---");
     printf("\n");
    
     for (row = 0; row < max_rows; row++) {
         printf("%3d |", row);
        
         for (col = 0; col < max_cols; col++) {
             int index = row * width + col;
             printf("%3d ", data[index]);
         }
        
         if (width > max_cols) printf("...");
         printf("\n");
     }
    
     if (height > max_rows) {
         printf("     ...\n");
     }
    
     printf("\n");
 }
 
 /* ===================================================================
  * LÓGICA DO MENU
  * =================================================================== */
 
 void exibir_menu(int inicializada, int img_carregada_c, int img_enviada_fpga) {
     printf("\n\n=== MENU DE TESTE DA API ===\n");
     printf("ESTADO: API [%s] | Buffer C [%s] | FPGA VRAM [%s]\n",
            inicializada ? "ON" : "OFF",
            img_carregada_c ? "CARREGADA" : "VAZIA",
            img_enviada_fpga ? "CARREGADA" : "VAZIA");
     printf("----------------------------------------------------------\n");
    
 
     printf("\n--- Carga de Imagem (Buffer C) ---\n");
     printf(" 1. Carregar Imagem BMP\n");
     printf(" 2. Gerar Gradiente\n");
     printf(" 3. Ler janela da FPGA e exibir matriz\n");
    
     printf("\n--- Comandos/Algoritmos (FPGA) ---\n");
     printf(" 4. NearestNeighbor\n");
     printf(" 5. PixelReplication\n");
     printf(" 6. Decimation\n");
     printf(" 7. BlockAveraging\n");
     printf(" 8. Atualizar (ASM_Refresh)\n");
     printf(" 9. RESET\n");
     printf(" 10. Trocar para a memoria primaria\n");
     printf(" 11. Trocar para a memoria secundaria\n");
     printf(" 12. Leitura do mouse\n");
    
     printf("\n----------------------------------------------------------\n");
     printf(" 0. Encerrar API e Sair\n");
    
     printf("\nEscolha uma opcao: ");
 }
 
 void limpar_buffer_entrada(void) {
     int c;
     while ((c = getchar()) != '\n' && c != EOF);
 }
 
 void esperar_enter(void) {
     printf("\nPressione Enter para continuar...");
     limpar_buffer_entrada();
     getchar();
 }
 
 void reset_temporario(void) {
     ASM_Reset();
     ASM_Pulse_Enable();
 }
 
 /* ===================================================================
  * MAIN INTERATIVO
  * =================================================================== */
 int main(void) {
     int api_inicializada = 0;
     int img_carregada_c = 0;
     int img_enviada_fpga = 0;
     int flag_nivel_zoom = 0;
    
     int opcao;
     char nome_arquivo[256];

     //mouse
     char device_path[MAX_PATH_LEN] = {0};
     char device_name[MAX_PATH_LEN] = {0};
     Cursor current_cursor = {0, 0}; // Posição inicial do cursor virtual
     MouseEvent event;
     int read_status;
     int canto1x1;
     int canto1y1;
     int canto2x2;
     int canto2y2;
     int selected_area = 0;
     //mouse
 
     uint8_t *image_data = malloc(IMG_WIDTH * IMG_HEIGHT);
     if (!image_data) {
         printf("ERRO FATAL: Nao foi possivel alocar memoria para a imagem!\n");
         return 1;
     }
     if (api_inicializada) {
         printf("AVISO: A API ja esta inicializada.\n");
     } else {
         printf("=== PASSO 1: Inicializando API (API_initialize) ===\n");
         void *result = API_initialize();
         if (result == (void*)-1 || result == (void*)-2) {
             printf("ERRO FATAL: API_initialize falhou. Verifique o sudo e o mmap.\n");
             free(image_data);
             return -1;
         }
         printf(">>> SUCESSO: API inicializada.\n");
         api_inicializada = 1;
     }
     
     
     while (1) {
         exibir_menu(api_inicializada, img_carregada_c, img_enviada_fpga);
        
         if (scanf("%d", &opcao) != 1) {
             limpar_buffer_entrada();
             printf("Erro: Entrada invalida. Por favor, digite um numero.\n");
             esperar_enter();
             continue;
         }
 
         printf("\n");
 
         switch (opcao) {
 
             case 1: // Carregar BMP
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                
                 printf("=== PASSO 2: Carregando Imagem BMP ===\n");
                 printf("Digite o nome do arquivo BMP: ");
                 scanf("%255s", nome_arquivo);
                //teste
                 printf("eu vou comer laranjas e bananas");
                 if (load_bmp(nome_arquivo, image_data) == 0) {
                     printf(">>> SUCESSO: Imagem BMP carregada no buffer C.\n");
                     img_carregada_c = 1;
                     img_enviada_fpga = 0;
                    
                     if (ASM_Get_Flag_Min_Zoom() == 1) {
                         printf("AVISO: Flag Min_Zoom ativa. Considere fazer Reset.\n");
                     }
                    
                     printf("=== PASSO 3: Enviando Imagem para FPGA ===\n");
                     if (enviar_imagem_para_fpga(image_data) == 0) {
                         printf(">>> SUCESSO: Imagem enviada para a VRAM do FPGA.\n");
                         img_enviada_fpga = 1;
                         flag_nivel_zoom = 0;
                     } else {
                         printf("ERRO FATAL: Falha ao enviar imagem para o FPGA.\n");
                         goto cleanup_error;
                     }
                 } else {
                     printf("ERRO: Nao foi possivel carregar o BMP.\n");
                 }
                 break;
 
             case 2: // Gerar Gradiente
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                
                 printf("=== PASSO 2: Gerando Gradiente ===\n");
                 gerar_padrao_teste(image_data);
                 printf(">>> SUCESSO: Gradiente gerado no buffer C.\n");
                 img_carregada_c = 1;
                 img_enviada_fpga = 0;
                
                 if (ASM_Get_Flag_Min_Zoom() == 1) {
                     printf("AVISO: Flag Min_Zoom ativa. Considere fazer Reset.\n");
                 }
                
                 printf("=== PASSO 3: Enviando Imagem para FPGA ===\n");
                 if (enviar_imagem_para_fpga(image_data) == 0) {
                     printf(">>> SUCESSO: Imagem enviada para a VRAM do FPGA.\n");
                     img_enviada_fpga = 1;
                     flag_nivel_zoom = 0;
                 } else {
                     printf("ERRO FATAL: Falha ao enviar imagem para o FPGA.\n");
                     goto cleanup_error;
                 }
                 break;
 
             case 3: { // Ler janela da FPGA
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
 
                 printf("\n=== PASSO 4: Ler VRAM e Printar Matriz ===\n");
                
                 int x1, y1, x2, y2;
                 int rx, ry, rw, rh;
                
                 printf("Digite a primeira coordenada (x1 y1): ");
                 if (scanf("%d %d", &x1, &y1) != 2) {
                     limpar_buffer_entrada();
                     printf("Erro na entrada.\n");
                     break;
                 }
                
                 printf("Digite a segunda coordenada (x2 y2): ");
                 if (scanf("%d %d", &x2, &y2) != 2) {
                     limpar_buffer_entrada();
                     printf("Erro na entrada.\n");
                     break;
                 }
 
                 rx = (x1 < x2) ? x1 : x2;
                 ry = (y1 < y2) ? y1 : y2;
                 rw = abs(x2 - x1);
                 rh = abs(y2 - y1);
 
                 if (rw == 0 || rh == 0) {
                     printf("ERRO: Area selecionada e vazia.\n");
                     break;
                 }
 
                 if (rx < 0 || ry < 0 || (rx + rw > IMG_WIDTH) || (ry + rh > IMG_HEIGHT)) {
                     printf("ERRO: Coordenadas fora dos limites da imagem (%dx%d).\n",
                            IMG_WIDTH, IMG_HEIGHT);
                     printf("      Area calculada: Inicio(%d,%d) Tamanho[%dx%d]\n", rx, ry, rw, rh);
                     break;
                 }
 
                 uint8_t *buffer_recorte = (uint8_t*)malloc(rw * rh);
                 if (!buffer_recorte) {
                     printf("ERRO: Memoria insuficiente.\n");
                     break;
                 }
 
                 if (read_fpga_window(buffer_recorte, rx, ry, rw, rh) == 0) {
                     printf(">>> SUCESSO: Dados lidos da area (%d,%d) a (%d,%d).\n",
                            rx, ry, rx+rw, ry+rh);
                     print_matrix(buffer_recorte, rw, rh);
                 } else {
                     printf("ERRO: Falha na leitura da FPGA.\n");
                 }
 
                 free(buffer_recorte);
                 break;
             }
 
             case 4: // NearestNeighbor
             case 5: { // PixelReplication
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 if (!img_enviada_fpga) {
                     printf("ERRO: Nenhuma imagem na VRAM do FPGA. Carregue uma imagem primeiro (Opcao 2 ou 3).\n");
                     break;
                 }
 
                 if (flag_nivel_zoom >= 3) {
                     printf("ERRO: Nivel maximo de Zoom IN atingido.\n");
                     printf("   Nao e possivel executar mais algoritmos de Zoom IN.\n");
                     printf("   Tente um 'Zoom OUT' (7, 8) ou 'Reset' (10).\n");
                     break;
                 }
                 if (ASM_Get_Flag_Max_Zoom() == 1) {
                     printf("ERRO: A 'Flag de Uso Maximo' (Max_Zoom) esta ATIVA.\n");
                     printf("   Nao e possivel executar mais algoritmos de Zoom IN.\n");
                     printf("   Tente um 'Zoom OUT' (7, 8) ou 'Reset' (10).\n");
                     break;
                 }
                
                 printf("=== PASSO 4: Executando Algoritmo (Zoom IN) ===\n");
                 int resultado;
                 if (opcao == 5) resultado = executar_algoritmo("NearestNeighbor", &NearestNeighbor);
                 else resultado = executar_algoritmo("PixelReplication", &PixelReplication);
                
                 if (resultado == 0) {
                     flag_nivel_zoom++;
                 } else {
                     printf("ERRO FATAL: Falha na execucao do algoritmo.\n");
                     goto cleanup_error;
                 }
                 break;
             }
 
             case 6: // Decimation
             case 7: { // BlockAveraging
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 if (!img_enviada_fpga) {
                     printf("ERRO: Nenhuma imagem na VRAM do FPGA. Carregue uma imagem primeiro (Opcao 2 ou 3).\n");
                     break;
                 }
 
                 if (flag_nivel_zoom <= -3) {
                     printf("ERRO: Nivel minimo de Zoom OUT atingido.\n");
                     printf("   Nao e possivel executar mais algoritmos de Zoom OUT.\n");
                     printf("   Tente um 'Zoom IN' (5, 6) ou 'Reset' (10).\n");
                     break;
                 }
                 if (ASM_Get_Flag_Min_Zoom() == 1) {
                     printf("ERRO: A 'Flag de Zoom Minimo' (Min_Zoom) esta ATIVA.\n");
                     printf("   Nao e possivel executar mais algoritmos de Zoom OUT.\n");
                     printf("   Tente um 'Zoom IN' (5, 6) ou 'Reset' (10).\n");
                     break;
                 }
 
                 printf("=== PASSO 4: Executando Algoritmo (Zoom OUT) ===\n");
                 int resultado_out;
                 if (opcao == 7) resultado_out = executar_algoritmo("Decimation", &Decimation);
                 else resultado_out = executar_algoritmo("BlockAveraging", &BlockAveraging);
                
                 if (resultado_out == 0) {
                     flag_nivel_zoom--;
                 } else {
                     printf("ERRO FATAL: Falha na execucao do algoritmo.\n");
                     goto cleanup_error;
                 }
                 break;
             }
 
             case 8: // Refresh
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 printf("=== EXECUTANDO: ASM_Refresh ===\n");
                 printf("   [C] Enviando comando ASM_Refresh() (NOP)...\n");
                 ASM_Refresh();
                 usleep(10000);
                 printf("   [C] Comando Refresh enviado.\n");
                 break;
 
             case 9: // Reset
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 printf("=== EXECUTANDO: RESET ===\n");
                 ASM_Reset();
                 flag_nivel_zoom = 0;
                 reset_temporario();
                 printf("   [C] Reset concluido. Flags zeradas.\n");
                 break;
 
             case 10: // Memoria Primaria
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 printf("=== EXECUTANDO: Troca para Memoria Primaria ===\n");
                 ASM_SetPrimaryMemory();
                 ASM_Refresh();
                 printf("   [C] Troca para memoria primaria realizada.\n");
                 break;
 
             case 11: // Memoria Secundaria
                 if (!api_inicializada) {
                     printf("ERRO: Inicialize a API primeiro (Opcao 1).\n");
                     break;
                 }
                 printf("=== EXECUTANDO: Troca para Memoria Secundaria ===\n");
                 ASM_SetSecondaryMemory();
                 ASM_Refresh();
                 printf("   [C] Troca para memoria secundaria realizada.\n");
                 break;
             case 12: // Leitura do mouse
                printf("Tentando encontrar e abrir o dispositivo do mouse...\n");

                mouse_fd_global = find_and_open_mouse(device_path, device_name);

                if (mouse_fd_global < 0) {
                    // Se a busca falhar, a função find_and_open_mouse já imprimiu o erro.
                    // O erro comum é "Permissão negada" (executar como root/sudo) ou
                    // "Não existe tal arquivo ou diretório" (se não houver /dev/input).
                    if (mouse_fd_global == -1 && errno == EACCES) {
                        fprintf(stderr, "\n!!! ERRO: Permissão negada. Tente executar como root (sudo).\n");
                    } else {
                        fprintf(stderr, "\n!!! ERRO: Não foi possível encontrar/abrir um mouse válido.\n");
                    }
                    return EXIT_FAILURE;
                }

                printf("\n[SUCESSO] Dispositivo Aberto (fd: %d):\n", mouse_fd_global);
                printf("  Caminho: %s\n", device_path);
                printf("  Nome:    %s\n", device_name);
                
                //limpeza da variáveis
                canto1x1 = 0;
                canto1y1 = 0;
                canto2x2 = 0;
                canto2y2 = 0;
                selected_area = 0;
                while (selected_area != 1) {
                    // Tenta ler e processar um evento
                    read_status = read_and_process_mouse_event(mouse_fd_global, &current_cursor, &event);
            
                    if (read_status < 0) {
                        // Erro de leitura (read_and_process_mouse_event já imprime o erro)
                        break;
                    } else if (read_status == 0) {
                        // EOF/Loop deve parar (não deve ocorrer em dispositivos de evento)
                        printf("Fim do arquivo (EOF).\n");
                        break;
                    } else if (read_status == 1) {
                        // Evento válido lido e processado
                        
                        // Exibir o evento
                        if (event.event_type == EV_REL) {
                            // Evento de movimento
                            printf("[MOVIMENTO] Tipo:%d | Code:%-12d | Valor:%-5d | CursorXY: (%d, %d)\n", event.event_type, event.event_code, event.event_value, event.cursor_pos.x, event.cursor_pos.y);
                        } else if (event.event_type == EV_KEY) {
                            // Evento de botão 272 esquerdo 273 direito
                            const char *state = (event.event_value == 1) ? "PRESSIONADO" : 
                                                (event.event_value == 0) ? "LIBERADO" : "REPETINDO";
                            printf("[BOTÃO]     T:%d | C:%-12d | V:%-5d (%s)\n", 
                                    event.event_type, event.event_code, event.event_value, state);
                                    if (event.event_code == 272){
                                        printf("Esquerdo");
                                        canto1x1 = event.cursor_pos.x;
                                        canto1y1 = event.cursor_pos.y;
                                        printf("C1X %d, C1Y %d", canto1x1, canto1y1);
                                    }
                                    if (event.event_code == 273){
                                        printf("Direito");
                                        canto2x2 = event.cursor_pos.x;
                                        canto2y2 = event.cursor_pos.y;
                                        printf("C2X %d, C2Y %d", canto2x2, canto2y2);
                                        selected_area = 1;
                                    }
                        }
                    }
                }
                break;
 
             case 0: // Sair
                selected_area = 0;
                 printf("=== PASSO FINAL: Encerrando ===\n");
                 if (api_inicializada) {
                     printf("Encerrando API...\n");
                     API_close();
                 }
                 free(image_data);
                 printf("Saindo.\n");
                 return 0;
 
             default:
                 printf("ERRO: Opcao invalida. Tente novamente.\n");
                 break;
         }
 
         esperar_enter();
     }
 
 cleanup_error:
     printf("\n!!! OCORREU UM ERRO FATAL. ENCERRANDO API. !!!\n");
     if (api_inicializada) {
         API_close();
     }
     free(image_data);
     return -1;
 }
 
 