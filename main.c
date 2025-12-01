#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

// Inclui o cabeçalho com as funções e estruturas a serem testadas
#include "mouse_utils.h" 

// Tamanho máximo do buffer de caminho/nome (usado em mouse_utils.c, aqui também)
#define MAX_PATH_LEN 256

// Variável global para o descritor de arquivo do mouse para que o handler possa fechar.
int mouse_fd_global = -1;

/**
 * @brief Função de handler de sinal (SIGINT/Ctrl+C) para fechar o descritor de arquivo
 * e garantir uma saída limpa.
 */
void sigint_handler(int sig) {
    if (mouse_fd_global != -1) {
        printf("\n\nSinal de interrupção recebido. Fechando o dispositivo...\n");
        close(mouse_fd_global);
    }
    exit(0);
}

/**
 * @brief Função principal para testar as funcionalidades do mouse.
 */
int main(void) {
    char device_path[MAX_PATH_LEN] = {0};
    char device_name[MAX_PATH_LEN] = {0};
    Cursor current_cursor = {0, 0}; // Posição inicial do cursor virtual
    MouseEvent event;
    int read_status;
    int canto1x1;
    int canto1y1;
    int canto2x2;
    int canto2y2;
    

    // 1. Configura o handler de sinal para Ctrl+C
    signal(SIGINT, sigint_handler);

    // 2. Teste da função find_and_open_mouse (Varredura e Abertura)
    // A função is_mouse é testada internamente por find_and_open_mouse.
    printf("===================================================\n");
    printf("   INÍCIO DO TESTE DE FUNCIONALIDADES DO MOUSE\n");
    printf("===================================================\n");
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
    printf("---------------------------------------------------\n");
    printf("   Comece a mover o mouse e clicar para ver os eventos!\n");
    printf("   Pressione Ctrl+C para sair.\n");
    printf("---------------------------------------------------\n");


    // 3. Teste da função read_and_process_mouse_event (Loop de Leitura)
    while (1) {
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
                printf("[MOVIMENTO] Tipo:%d | Code:%-12d | Valor:%-5d | CursorXY: (%d, %d)\n", 
                        event.event_type, event.event_code, event.event_value, 
                        event.cursor_pos.x, event.cursor_pos.y);
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
                        }
            } else if (event.event_type == EV_SYN && event.event_code == SYN_REPORT) {
                // Evento de Sincronização (geralmente ignorado, mas útil para ver)
                // printf("[SINCRONIA] T:%d | C:%-12d | V:%-5d\n", 
                //         event.event_type, event.event_code, event.event_value);
            } else {
                 // Outros eventos (e.g., EV_MSC)
                printf("[OUTRO]     T:%d | C:%-12d | V:%-5d\n", 
                        event.event_type, event.event_code, event.event_value);
            }
        }
    }

    // Fecha o descritor de arquivo se sairmos do loop por erro/EOF
    if (mouse_fd_global != -1) {
        close(mouse_fd_global);
    }
    printf("\nTeste finalizado.\n");
    
    return EXIT_SUCCESS;
}