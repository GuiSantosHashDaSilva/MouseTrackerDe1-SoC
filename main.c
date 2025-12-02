/*
 * =========================================================================
 * main_menu_test.c: COMPLETE FINAL VERSION - INTEGRATED WITH MOUSE
 * =========================================================================
 * 
 * Image Processing System with FPGA Hardware Acceleration
 * Supports BMP loading, zoom algorithms, mouse input, and memory management
 * 
 */

#include "api.h"
#include "mouse_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <termios.h>

/* ===================================================================
 * CONSTANTS AND CONFIGURATION
 * =================================================================== */
#define TIMEOUT_LOOPS 50
#define REFRESH_DELAY_US 100000
#define PULSE_DELAY_US 10000
#define MAX_PATH_LEN 256

/* Zoom Level Constraints */
#define MAX_ZOOM_IN_LEVEL 3
#define MIN_ZOOM_OUT_LEVEL -3

/* Mouse button codes */
#define BTN_LEFT_CODE 272
#define BTN_RIGHT_CODE 273

/* Zoom operation types */
#define ZOOM_IN  '+'
#define ZOOM_OUT '-'

/* Maximum zoom levels */
#define MAX_REGIONAL_ZOOM_LEVELS 3

/* Regional zoom context */
typedef struct {
    int x;              /* Top-left X coordinate */
    int y;              /* Top-left Y coordinate */
    int width;          /* Width of region */
    int height;         /* Height of region */
    int base_width;     /* Original width */
    int base_height;    /* Original height */
    int zoom_level;     /* Current zoom level (0 = original) */
    
    /* Buffer stack to store each zoom state */
    uint8_t *zoom_buffers[MAX_REGIONAL_ZOOM_LEVELS];
    int buffer_sizes[MAX_REGIONAL_ZOOM_LEVELS];
} RegionalZoomContext;

/* Global mouse file descriptor */
int mouse_fd_global = -1;

/* ===================================================================
 * BMP FILE STRUCTURES
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

/* ===================================================================
 * UTILITY FUNCTIONS
 * =================================================================== */

/**
 * @brief Converts RGB color to grayscale using standard luminosity formula
 */
uint8_t rgb_to_gray(uint8_t r, uint8_t g, uint8_t b) {
    return (uint8_t)((299 * r + 587 * g + 114 * b) / 1000);
}

/**
 * @brief Clears input buffer to prevent scanf issues
 */
void clear_input_buffer(void) {
    int c;
    while ((c = getchar()) != '\n' && c != EOF);
}

/**
 * @brief Waits for user to press Enter
 */
void wait_for_enter(void) {
    printf("\nPressione Enter para continuar...");
    clear_input_buffer();
    getchar();
}

/**
 * @brief Lê um único caractere do teclado sem esperar o Enter
 * @return O caractere lido
 */
char read_key_direct(void) {
    struct termios oldt, newt;
    char ch;

    // 1. Pega os atributos atuais do terminal
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;

    // 2. Desabilita o modo canônico (buffer de linha) e o echo (impressão na tela)
    newt.c_lflag &= ~(ICANON | ECHO);

    // 3. Aplica as novas configurações imediatamente
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);

    // 4. Lê o caractere
    ch = getchar();

    // 5. Restaura as configurações originais
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);

    return ch;
}

/* ===================================================================
 * BMP IMAGE LOADING
 * =================================================================== */

/**
 * @brief Loads a BMP image file into memory buffer
 * @param filename Path to BMP file
 * @param image_data Output buffer for grayscale pixel data
 * @return 0 on success, -1 on failure
 */
int load_bmp(const char *filename, uint8_t *image_data) {
    FILE *file;
    BMPHeader header;
    BMPInfoHeader info_header;
   
    file = fopen(filename, "rb");
    if (!file) {
        printf(" Erro ao abrir '%s'\n", filename);
        return -1;
    }
   
    /* Read and validate BMP header */
    fread(&header, sizeof(BMPHeader), 1, file);
    if (header.type != 0x4D42) {
        printf(" Arquivo nao e BMP valido (assinatura 0x%X)\n", header.type);
        fclose(file);
        return -1;
    }
   
    /* Read and validate info header */
    fread(&info_header, sizeof(BMPInfoHeader), 1, file);
    if (info_header.width != IMG_WIDTH || abs(info_header.height) != IMG_HEIGHT) {
        printf(" Dimensao incorreta: %dx%d (esperado %dx%d)\n",
               info_header.width, abs(info_header.height), IMG_WIDTH, IMG_HEIGHT);
        fclose(file);
        return -1;
    }
   
    /* Position file pointer at pixel data */
    fseek(file, header.offset, SEEK_SET);
   
    /* Calculate row size with padding */
    int row_size = ((info_header.width * info_header.bits + 31) / 32) * 4;
   
    uint8_t *row_data = (uint8_t*)malloc(row_size);
    if (!row_data) {
        printf("\nErro ao alocar memoria para a linha do BMP\n");
        fclose(file);
        return -1;
    }
   
    /* Read and convert pixel data */
    printf("Carregando");
    for (int y = 0; y < IMG_HEIGHT; y++) {
        fread(row_data, 1, row_size, file);
       
        for (int x = 0; x < IMG_WIDTH; x++) {
            uint8_t gray;
           
            /* Handle different bit depths */
            if (info_header.bits == 32) {
                uint8_t b = row_data[x * 4 + 0];
                uint8_t g = row_data[x * 4 + 1];
                uint8_t r = row_data[x * 4 + 2];
                gray = rgb_to_gray(r, g, b);
            } else if (info_header.bits == 24) {
                uint8_t b = row_data[x * 3 + 0];
                uint8_t g = row_data[x * 3 + 1];
                uint8_t r = row_data[x * 3 + 2];
                gray = rgb_to_gray(r, g, b);
            } else if (info_header.bits == 8) {
                gray = row_data[x];
            } else {
                printf("\nFormato %d bits nao suportado\n", info_header.bits);
                free(row_data);
                fclose(file);
                return -1;
            }
           
            /* BMP rows are stored bottom-to-top */
            int addr = (IMG_HEIGHT - 1 - y) * IMG_WIDTH + x;
            image_data[addr] = gray;
        }
       
        /* Progress indicator */
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
 * TEST PATTERN GENERATION
 * =================================================================== */

/**
 * @brief Generates a horizontal gradient test pattern
 */
void generate_test_pattern(uint8_t *image_data) {
    printf("   [C] Gerando padrao de teste (gradiente 320x240)...\n");
    for (int y = 0; y < IMG_HEIGHT; y++) {
        for (int x = 0; x < IMG_WIDTH; x++) {
            uint8_t pixel_value = (uint8_t)((x * 255) / (IMG_WIDTH - 1));
            image_data[y * IMG_WIDTH + x] = pixel_value;
        }
    }
}

/* ===================================================================
 * FPGA COMMUNICATION
 * =================================================================== */

/**
 * @brief Sends entire image buffer to FPGA VRAM (Primary Memory)
 * @param image_data Source pixel buffer
 * @return 0 on success, -1 on failure
 */
int send_image_to_fpga(uint8_t *image_data) {
    int total_pixels = IMG_WIDTH * IMG_HEIGHT;
    int errors = 0;

    printf("   [C] Enviando %d pixels para o FPGA (testando ASM_Store)...\n", total_pixels);
   
    for (int i = 0; i < total_pixels; i++) {
        int status = ASM_Store(i, image_data[i], 0); // mem_sel = 0 (Primary Memory)
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
    usleep(REFRESH_DELAY_US);

    if (errors > 0) {
        printf("   [C] ERRO: %d falhas de escrita de pixel.\n", errors);
        return -1;
    }
    return 0;
}

/**
 * @brief Executes an algorithm on FPGA and waits for completion
 * @param algo_name Algorithm name for logging
 * @param algo_func Pointer to algorithm function
 * @return 0 on success, -1 on failure
 */
int execute_algorithm(const char *algo_name, void (*algo_func)(void)) {
    printf("   [C] Executando '%s' (assincrono)...\n", algo_name);
   
    /* Set algorithm opcode */
    algo_func();
   
    printf("   [C] Hardware iniciado. Aguardando FLAG_DONE (polling)...\n");

    /* Poll for completion */
    int timeout_counter = 0;
    while (ASM_Get_Flag_Done() == 0) {
        usleep(REFRESH_DELAY_US);
        timeout_counter++;
       
        if (timeout_counter > TIMEOUT_LOOPS) {
            printf("\n   [C] ERRO FATAL: TIMEOUT DO ALGORITMO '%s'!\n", algo_name);
            return -1;
        }
    }
   
    printf("   [C] FLAG_DONE recebida para '%s'!\n", algo_name);

    /* Check for hardware errors */
    if (ASM_Get_Flag_Error() != 0) {
        printf("   [C] ATENCAO: O FPGA reportou um ERRO (Flag_Error) durante '%s'!\n", algo_name);
        return -1;
    }

    /* Pulse enable to acknowledge completion */
    ASM_Pulse_Enable();
   
    printf("   [C] '%s' executado com sucesso.\n", algo_name);
    return 0;
}

/**
 * @brief Reads a rectangular window from FPGA memory
 * @param buffer Output buffer for pixel data
 * @param x Starting X coordinate
 * @param y Starting Y coordinate
 * @param width Window width
 * @param height Window height
 * @param mem_sel Memory selector (0=Primary, 1=Secondary)
 * @return 0 on success, -1 on failure
 */

int read_fpga_window(uint8_t *buffer, int x, int y, int width, int height, int mem_sel) {
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
   
    printf("   [C] Lendo janela (%d,%d) com tamanho %dx%d da FPGA (Memoria %d)...\n",
           x, y, width, height, mem_sel);
   
    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int addr = (y + row) * IMG_WIDTH + (x + col);
            int pixel_value = ASM_Load(addr, mem_sel);
           
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

/**
 * @brief Prints a matrix of pixel values to console
 */
void print_matrix(const uint8_t *data, int width, int height) {
    if (!data) {
        printf("ERRO: Dados nulos em print_matrix\n");
        return;
    }
   
    printf("\n=== MATRIZ %dx%d ===\n", width, height);
   
    /* Limit display size for readability */
    int max_rows = (height > 20) ? 20 : height;
    int max_cols = (width > 40) ? 40 : width;
   
    if (height > max_rows || width > max_cols) {
        printf("(Mostrando apenas %dx%d da matriz completa)\n\n", max_cols, max_rows);
    }
   
    /* Print column headers */
    printf("     ");
    for (int col = 0; col < max_cols; col++) {
        printf("%3d ", col);
    }
    if (width > max_cols) printf("...");
    printf("\n");
   
    printf("     ");
    for (int col = 0; col < max_cols; col++) {
        printf("----");
    }
    if (width > max_cols) printf("---");
    printf("\n");
   
    /* Print matrix data */
    for (int row = 0; row < max_rows; row++) {
        printf("%3d |", row);
       
        for (int col = 0; col < max_cols; col++) {
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
 * REGIONAL ZOOM FUNCTION
 * =================================================================== */

/**
 * @brief Performs regional zoom on a user-selected area
 * @param image_data Original image buffer (for restore after overlay)
 * @return 0 on success, -1 on failure
 */

int regional_zoom_start(RegionalZoomContext *ctx) {
    int corner1_x, corner1_y, corner2_x, corner2_y;
    
    printf("\n=== INICIO DO ZOOM REGIONAL ===\n");
    
    /* Capture area using standard mouse function */
    if (capture_mouse_area(&corner1_x, &corner1_y, &corner2_x, &corner2_y) != 0) {
        printf("ERRO: Falha na captura da area.\n");
        return -1;
    }
    
    /* Normalize coordinates */
    ctx->x = (corner1_x < corner2_x) ? corner1_x : corner2_x;
    ctx->y = (corner1_y < corner2_y) ? corner1_y : corner2_y;
    ctx->width = abs(corner2_x - corner1_x);
    ctx->height = abs(corner2_y - corner1_y);
    
    /* Validate selection */
    if (ctx->width == 0 || ctx->height == 0) {
        printf("ERRO: Area selecionada e vazia.\n");
        return -1;
    }
    
    if (ctx->x < 0 || ctx->y < 0 || 
        ctx->x + ctx->width > IMG_WIDTH || ctx->y + ctx->height > IMG_HEIGHT) {
        printf("ERRO: Area selecionada fora dos limites.\n");
        return -1;
    }
    
    /* Initialize zoom state */
    ctx->base_width = ctx->width;
    ctx->base_height = ctx->height;
    ctx->zoom_level = 0;
    
    /* Initialize buffer stack */
    for (int i = 0; i < MAX_REGIONAL_ZOOM_LEVELS; i++) {
        ctx->zoom_buffers[i] = NULL;
        ctx->buffer_sizes[i] = 0;
    }
    
    printf("\nJanela regional configurada:\n");
    printf("  Posicao: (%d, %d)\n", ctx->x, ctx->y);
    printf("  Tamanho: %dx%d\n", ctx->width, ctx->height);
    printf("  Max zoom levels: %d\n", MAX_REGIONAL_ZOOM_LEVELS);
    
    return 0;
}

/* ===================================================================
 * REGIONAL ZOOM CLEANUP
 * =================================================================== */

void regional_zoom_cleanup(RegionalZoomContext *ctx) {
    printf("\nLimpando buffers de zoom...\n");
    for (int i = 0; i < MAX_REGIONAL_ZOOM_LEVELS; i++) {
        if (ctx->zoom_buffers[i] != NULL) {
            free(ctx->zoom_buffers[i]);
            ctx->zoom_buffers[i] = NULL;
            printf("  Buffer nivel %d liberado\n", i);
        }
    }
}

/* ===================================================================
 * REGIONAL ZOOM APPLY
 * =================================================================== */

int regional_zoom_apply(RegionalZoomContext *ctx, uint8_t *image_data, 
                       int global_zoom_level, int operation) {
    
    /* Validate zoom out limit */
    if (operation == ZOOM_OUT && ctx->zoom_level <= 0) {
        printf("\nLimite de zoom-out atingido (resolucao original).\n");
        return 0;
    }
    
    /* Validate zoom in limit */
    if (operation == ZOOM_IN && ctx->zoom_level >= MAX_REGIONAL_ZOOM_LEVELS - 1) {
        printf("\nLimite maximo de zoom-in atingido (%d niveis).\n", MAX_REGIONAL_ZOOM_LEVELS);
        return 0;
    }
    
    printf("\n=== APLICANDO ZOOM %s ===\n", (operation == ZOOM_IN) ? "IN" : "OUT");
    printf("Zoom level da janela: %d | Zoom global: %d\n", ctx->zoom_level, global_zoom_level);
    
    /* ============================================================
     * ZOOM OUT: RESTAURAR BUFFER ANTERIOR
     * ============================================================ */
    if (operation == ZOOM_OUT) {
        printf("\n[ZOOM OUT] Restaurando buffer do nivel %d...\n", ctx->zoom_level - 1);
        
        int prev_level = ctx->zoom_level - 1;
        
        if (ctx->zoom_buffers[prev_level] == NULL) {
            printf("ERRO: Buffer do nivel anterior nao existe!\n");
            return -1;
        }
        
        /* Determinar memória de onde ler o background */
        int bg_mem = (prev_level == 0 && global_zoom_level > 0) ? 1 : 0;
        
        /* Salvar background completo */
        uint8_t *full_background = (uint8_t*)malloc(IMG_WIDTH * IMG_HEIGHT);
        if (!full_background) {
            printf("ERRO: Falha ao alocar background.\n");
            return -1;
        }
        
        printf("  Salvando background da memoria %d...\n", bg_mem);
        for (int addr = 0; addr < IMG_WIDTH * IMG_HEIGHT; addr++) {
            full_background[addr] = (uint8_t)ASM_Load(addr, bg_mem);
        }
        
        /* Reset e restaurar */
        printf("  RESET e restaurando estado anterior...\n");
        ASM_Reset();
        ASM_Pulse_Enable();
        usleep(PULSE_DELAY_US);
        
        /* Restaurar background */
        for (int addr = 0; addr < IMG_WIDTH * IMG_HEIGHT; addr++) {
            ASM_Store(addr, full_background[addr], 0);
        }
        
        /* Sobrepor buffer do nível anterior */
        printf("  Sobrepondo buffer do nivel %d na posicao (%d,%d)...\n",
               prev_level, ctx->x, ctx->y);
        
        for (int row = 0; row < ctx->height; row++) {
            for (int col = 0; col < ctx->width; col++) {
                int img_addr = (ctx->y + row) * IMG_WIDTH + (ctx->x + col);
                int buf_idx = row * ctx->width + col;
                ASM_Store(img_addr, ctx->zoom_buffers[prev_level][buf_idx], 0);
            }
        }
        
        ASM_Refresh();
        usleep(REFRESH_DELAY_US);
        
        free(full_background);
        
        /* Liberar buffer do nível atual (não precisamos mais) */
        if (ctx->zoom_buffers[ctx->zoom_level] != NULL) {
            free(ctx->zoom_buffers[ctx->zoom_level]);
            ctx->zoom_buffers[ctx->zoom_level] = NULL;
        }
        
        /* Decrementar zoom level */
        ctx->zoom_level--;
        
        printf("\n>>> ZOOM OUT concluido! Nivel: %d\n\n", ctx->zoom_level);
        return 0;
    }
    
    /* ============================================================
     * ZOOM IN: PROCESSAR E SALVAR NO BUFFER
     * ============================================================ */
    
    /* Determinar memória de leitura */
    int read_mem = (ctx->zoom_level == 0 && global_zoom_level > 0) ? 1 : 0;
    
    /* Alocar buffers */
    uint8_t *full_background = (uint8_t*)malloc(IMG_WIDTH * IMG_HEIGHT);
    uint8_t *region_buffer = (uint8_t*)malloc(ctx->width * ctx->height);
    
    if (!full_background || !region_buffer) {
        printf("ERRO: Falha ao alocar memoria.\n");
        if (full_background) free(full_background);
        if (region_buffer) free(region_buffer);
        return -1;
    }
    
    /* PASSO 1: Salvar background completo */
    printf("\n[1/6] Salvando background da memoria %d...\n", read_mem);
    for (int addr = 0; addr < IMG_WIDTH * IMG_HEIGHT; addr++) {
        full_background[addr] = (uint8_t)ASM_Load(addr, read_mem);
    }
    
    /* PASSO 2: Extrair e SALVAR região atual no buffer ANTES de processar */
    printf("\n[2/6] Salvando estado atual no buffer (nivel %d)...\n", ctx->zoom_level);
    
    /* Alocar buffer para este nível */
    ctx->zoom_buffers[ctx->zoom_level] = (uint8_t*)malloc(ctx->width * ctx->height);
    if (ctx->zoom_buffers[ctx->zoom_level] == NULL) {
        printf("ERRO: Falha ao alocar buffer de zoom.\n");
        free(full_background);
        free(region_buffer);
        return -1;
    }
    ctx->buffer_sizes[ctx->zoom_level] = ctx->width * ctx->height;
    
    /* Salvar estado atual no buffer */
    for (int row = 0; row < ctx->height; row++) {
        for (int col = 0; col < ctx->width; col++) {
            int img_addr = (ctx->y + row) * IMG_WIDTH + (ctx->x + col);
            int pixel = ASM_Load(img_addr, read_mem);
            int buf_idx = row * ctx->width + col;
            
            ctx->zoom_buffers[ctx->zoom_level][buf_idx] = (uint8_t)pixel;
            region_buffer[buf_idx] = (uint8_t)pixel;
        }
    }
    printf("  Estado salvo: %d pixels no buffer[%d]\n", 
           ctx->buffer_sizes[ctx->zoom_level], ctx->zoom_level);
    
    /* PASSO 3: Reset e enviar região para FPGA */
    printf("\n[3/6] RESET e enviando regiao para FPGA...\n");
    ASM_Reset();
    ASM_Pulse_Enable();
    usleep(PULSE_DELAY_US);
    
    for (int row = 0; row < ctx->height; row++) {
        for (int col = 0; col < ctx->width; col++) {
            int fpga_addr = row * IMG_WIDTH + col;
            ASM_Store(fpga_addr, region_buffer[row * ctx->width + col], 0);
        }
    }
    ASM_Refresh();
    usleep(REFRESH_DELAY_US);
    
    /* PASSO 4: Executar NearestNeighbor */
    printf("\n[4/6] Executando NearestNeighbor...\n");
    NearestNeighbor();
    
    int timeout = 0;
    while (ASM_Get_Flag_Done() == 0) {
        usleep(REFRESH_DELAY_US);
        timeout++;
        if (timeout > TIMEOUT_LOOPS) {
            printf("ERRO: Timeout!\n");
            free(region_buffer);
            free(full_background);
            return -1;
        }
    }
    
    if (ASM_Get_Flag_Error() != 0) {
        printf("ERRO: Flag de erro!\n");
        free(region_buffer);
        free(full_background);
        return -1;
    }
    
    ASM_Pulse_Enable();
    printf("  NearestNeighbor concluido\n");
    
    /* PASSO 5: Ler resultado processado */
    printf("\n[5/6] Lendo resultado da Secondary...\n");
    for (int row = 0; row < ctx->height; row++) {
        for (int col = 0; col < ctx->width; col++) {
            int fpga_addr = row * IMG_WIDTH + col;
            region_buffer[row * ctx->width + col] = (uint8_t)ASM_Load(fpga_addr, 1);
        }
    }
    
    /* PASSO 6: Reset, restaurar background e sobrepor resultado */
    printf("\n[6/6] RESET, restaurando background e sobrepondo...\n");
    ASM_Reset();
    ASM_Pulse_Enable();
    usleep(PULSE_DELAY_US);
    
    for (int addr = 0; addr < IMG_WIDTH * IMG_HEIGHT; addr++) {
        ASM_Store(addr, full_background[addr], 0);
    }
    
    for (int row = 0; row < ctx->height; row++) {
        for (int col = 0; col < ctx->width; col++) {
            int img_addr = (ctx->y + row) * IMG_WIDTH + (ctx->x + col);
            ASM_Store(img_addr, region_buffer[row * ctx->width + col], 0);
        }
    }
    
    ASM_Refresh();
    usleep(REFRESH_DELAY_US);
    
    /* Incrementar zoom level */
    ctx->zoom_level++;
    
    free(region_buffer);
    free(full_background);
    
    printf("\n>>> ZOOM IN concluido! Nivel: %d\n\n", ctx->zoom_level);
    
    return 0;
}


/* ===================================================================
 * MOUSE AREA SELECTION
 * =================================================================== */

/**
 * @brief Captures two mouse clicks to define a rectangular area
 * @param corner1_x Output: First corner X coordinate
 * @param corner1_y Output: First corner Y coordinate
 * @param corner2_x Output: Second corner X coordinate
 * @param corner2_y Output: Second corner Y coordinate
 * @return 0 on success, -1 on failure
 */
int capture_mouse_area(int *corner1_x, int *corner1_y, int *corner2_x, int *corner2_y) {
    Cursor current_cursor = {0, 0};
    MouseEvent event;
    int read_status;
    int corners_captured = 0;
    
    printf("\n=== CAPTURA DE AREA COM MOUSE ===\n");
    printf("Instrucoes:\n");
    printf("  - Botao ESQUERDO: Define o primeiro canto\n");
    printf("  - Botao DIREITO: Define o segundo canto\n");
    printf("  - Mova o mouse para posicionar o cursor\n\n");
    
    while (corners_captured < 2) {
        read_status = read_and_process_mouse_event(mouse_fd_global, &current_cursor, &event);
        
        if (read_status < 0) {
            printf("ERRO: Falha na leitura do mouse\n");
            return -1;
        } else if (read_status == 0) {
            printf("ERRO: EOF ao ler mouse\n");
            return -1;
        } else if (read_status == 1) {
            /* Display movement events */
            if (event.event_type == EV_REL) {
                printf("[MOVIMENTO] CursorXY: (%d, %d)\r", 
                       event.cursor_pos.x, event.cursor_pos.y);
                fflush(stdout);
            }
            /* Handle button press events */
            else if (event.event_type == EV_KEY && event.event_value == 1) {
                if (event.event_code == BTN_LEFT_CODE && corners_captured == 0) {
                    *corner1_x = event.cursor_pos.x;
                    *corner1_y = event.cursor_pos.y;
                    printf("\n[CANTO 1] Capturado em (%d, %d)\n", *corner1_x, *corner1_y);
                    printf("Agora clique com o botao DIREITO para o segundo canto...\n");
                    corners_captured = 1;
                }
                else if (event.event_code == BTN_RIGHT_CODE && corners_captured == 1) {
                    *corner2_x = event.cursor_pos.x;
                    *corner2_y = event.cursor_pos.y;
                    printf("[CANTO 2] Capturado em (%d, %d)\n", *corner2_x, *corner2_y);
                    corners_captured = 2;
                }
            }
        }
    }
    
    printf("\n>>> Area capturada com sucesso!\n");
    return 0;
}

/* ===================================================================
 * MENU SYSTEM
 * =================================================================== */

/**
 * @brief Displays the interactive menu
 */
void display_menu(int image_loaded, int image_sent_to_fpga, int zoom_level) {
    printf("\n\n=== MENU DE TESTE DA API ===\n");
    printf("ESTADO: Buffer C [%s] | FPGA VRAM [%s] | Zoom Level [%d]\n",
           image_loaded ? "CARREGADA" : "VAZIA",
           image_sent_to_fpga ? "CARREGADA" : "VAZIA",
           zoom_level);
    printf("----------------------------------------------------------\n");
   
    printf("\n--- Carga de Imagem (Buffer C) ---\n");
    printf(" 1. Carregar Imagem BMP\n");
    printf(" 2. Gerar Gradiente\n");
    printf(" 3. Ler janela da FPGA (com mouse) e exibir matriz\n");
   
    printf("\n--- Algoritmos de Processamento (FPGA) ---\n");
    printf(" 4. NearestNeighbor (Zoom IN)\n");
    printf(" 5. PixelReplication (Zoom IN)\n");
    printf(" 6. Decimation (Zoom OUT)\n");
    printf(" 7. BlockAveraging (Zoom OUT)\n");
    printf(" 8. RESET\n");
    printf(" 9. Zoom Regional (selecionar area com mouse)\n");
   
    printf("\n----------------------------------------------------------\n");
    printf(" 0. Encerrar API e Sair\n");
   
    printf("\nEscolha uma opcao: ");
}

/**
 * @brief Checks if window reading is allowed and determines memory to read
 * @param zoom_level Current zoom level
 * @param mem_sel_out Output: Memory selector (0 or 1)
 * @return 1 if allowed, 0 if not allowed
 */
int can_read_window(int zoom_level, int *mem_sel_out) {
    /* Reading rules:
     * 1. If zoom_level > 0: Zoom-in was applied, image is processed
     *    -> Read from SECONDARY memory (mem_sel = 1)
     * 2. If zoom_level <= 0: At initial state or zoom-out was applied
     *    -> Read from PRIMARY memory (mem_sel = 0)
     * 3. Reading is NOT allowed if zoom-out was applied without returning to initial state
     */
    
    if (zoom_level < 0) {
        /* Zoom-out was applied and not compensated */
        printf("ERRO: Nao e possivel ler a janela no estado atual de zoom.\n");
        printf("   A leitura so e permitida quando:\n");
        printf("   - Nenhum algoritmo foi executado ainda (zoom_level = 0)\n");
        printf("   - Algoritmos de zoom-in foram usados (zoom_level > 0)\n");
        printf("   Nivel de zoom atual: %d\n", zoom_level);
        return 0;
    }
    
    /* Determine which memory to read */
    if (zoom_level > 0) {
        *mem_sel_out = 1; // Secondary memory (processed image)
        printf("   [INFO] Imagem processada detectada. Lendo da memoria SECUNDARIA.\n");
    } else {
        *mem_sel_out = 0; // Primary memory (original image)
        printf("   [INFO] Imagem original. Lendo da memoria PRIMARIA.\n");
    }
    
    return 1;
}

/* ===================================================================
 * MAIN PROGRAM
 * =================================================================== */
int main(void) {
    /* System state variables */
    int image_loaded_in_memory = 0;
    int image_sent_to_fpga = 0;
    int zoom_level = 0;
   
    int option;
    char filename[256];
    
    /* Mouse variables */
    char device_path[MAX_PATH_LEN] = {0};
    char device_name[MAX_PATH_LEN] = {0};

    /* Allocate image buffer */
    uint8_t *image_data = malloc(IMG_WIDTH * IMG_HEIGHT);
    if (!image_data) {
        printf("ERRO FATAL: Nao foi possivel alocar memoria para a imagem!\n");
        return 1;
    }

    /* ===================================================================
     * AUTOMATIC INITIALIZATION
     * =================================================================== */
    printf("=== INICIALIZANDO SISTEMA ===\n");
    printf("Inicializando API (API_initialize)...\n");
    
    void *init_result = API_initialize();
    if (init_result == (void*)-1 || init_result == (void*)-2) {
        printf("ERRO FATAL: API_initialize falhou. Verifique o sudo e o mmap.\n");
        free(image_data);
        return -1;
    }
    
    printf(">>> API inicializada com sucesso.\n");
    
    /* Reset FPGA to ensure clean state */
    printf("Executando reset inicial do FPGA...\n");
    ASM_Reset();
    ASM_Pulse_Enable();
    usleep(PULSE_DELAY_US);
    
    printf(">>> Sistema inicializado e pronto para uso.\n");
    
    /* Initialize mouse device */
    printf("\nInicializando dispositivo de mouse...\n");
    mouse_fd_global = find_and_open_mouse(device_path, device_name);
    
    if (mouse_fd_global < 0) {
        if (errno == EACCES) {
            printf("!!! AVISO: Permissao negada para mouse. Execute como root (sudo).\n");
            printf("    A funcionalidade de selecao com mouse estara desabilitada.\n");
        } else {
            printf("!!! AVISO: Nao foi possivel encontrar/abrir um mouse valido.\n");
            printf("    A funcionalidade de selecao com mouse estara desabilitada.\n");
        }
    } else {
        printf(">>> Mouse inicializado com sucesso:\n");
        printf("    Caminho: %s\n", device_path);
        printf("    Nome: %s\n", device_name);
    }
    
    wait_for_enter();

    /* ===================================================================
     * MAIN MENU LOOP
     * =================================================================== */
    while (1) {
        display_menu(image_loaded_in_memory, image_sent_to_fpga, zoom_level);
       
        if (scanf("%d", &option) != 1) {
            clear_input_buffer();
            printf("Erro: Entrada invalida. Por favor, digite um numero.\n");
            wait_for_enter();
            continue;
        }

        printf("\n");

        switch (option) {
            /* ==================== LOAD BMP IMAGE ==================== */
            case 1: {
                printf("=== CARREGANDO IMAGEM BMP ===\n");
                printf("Digite o nome do arquivo BMP: ");
                scanf("%255s", filename);
               
                if (load_bmp(filename, image_data) == 0) {
                    printf(">>> SUCESSO: Imagem BMP carregada no buffer C.\n");
                    image_loaded_in_memory = 1;
                    image_sent_to_fpga = 0;
                   
                    /* Check if reset is needed */
                    if (ASM_Get_Flag_Min_Zoom() == 1) {
                        printf("AVISO: Flag Min_Zoom ativa. Considere fazer Reset.\n");
                    }
                   
                    /* Send image to FPGA */
                    printf("=== ENVIANDO IMAGEM PARA FPGA ===\n");
                    if (send_image_to_fpga(image_data) == 0) {
                        printf(">>> SUCESSO: Imagem enviada para a VRAM do FPGA.\n");
                        image_sent_to_fpga = 1;
                        zoom_level = 0;
                    } else {
                        printf("ERRO FATAL: Falha ao enviar imagem para o FPGA.\n");
                        goto cleanup_error;
                    }
                } else {
                    printf("ERRO: Nao foi possivel carregar o BMP.\n");
                }
                break;
            }

            /* ==================== GENERATE TEST PATTERN ==================== */
            case 2: {
                printf("=== GERANDO GRADIENTE ===\n");
                generate_test_pattern(image_data);
                printf(">>> SUCESSO: Gradiente gerado no buffer C.\n");
                image_loaded_in_memory = 1;
                image_sent_to_fpga = 0;
               
                /* Check if reset is needed */
                if (ASM_Get_Flag_Min_Zoom() == 1) {
                    printf("AVISO: Flag Min_Zoom ativa. Considere fazer Reset.\n");
                }
               
                /* Send pattern to FPGA */
                printf("=== ENVIANDO IMAGEM PARA FPGA ===\n");
                if (send_image_to_fpga(image_data) == 0) {
                    printf(">>> SUCESSO: Imagem enviada para a VRAM do FPGA.\n");
                    image_sent_to_fpga = 1;
                    zoom_level = 0;
                } else {
                    printf("ERRO FATAL: Falha ao enviar imagem para o FPGA.\n");
                    goto cleanup_error;
                }
                break;
            }

            /* ==================== READ FPGA WINDOW WITH MOUSE ==================== */
            case 3: {
                if (!image_sent_to_fpga) {
                    printf("ERRO: Nenhuma imagem na VRAM do FPGA. Carregue uma imagem primeiro (Opcao 1 ou 2).\n");
                    break;
                }
                
                if (mouse_fd_global < 0) {
                    printf("ERRO: Mouse nao inicializado. Execute o programa com sudo.\n");
                    break;
                }

                /* Check if reading is allowed and determine memory selector */
                int mem_sel;
                if (!can_read_window(zoom_level, &mem_sel)) {
                    break;
                }

                printf("\n=== LER VRAM E PRINTAR MATRIZ (COM MOUSE) ===\n");
               
                /* Capture area with mouse */
                int corner1_x, corner1_y, corner2_x, corner2_y;
                if (capture_mouse_area(&corner1_x, &corner1_y, &corner2_x, &corner2_y) != 0) {
                    printf("ERRO: Falha na captura da area com o mouse.\n");
                    break;
                }
                
                /* Calculate rectangle boundaries */
                int rect_x = (corner1_x < corner2_x) ? corner1_x : corner2_x;
                int rect_y = (corner1_y < corner2_y) ? corner1_y : corner2_y;
                int rect_width = abs(corner2_x - corner1_x);
                int rect_height = abs(corner2_y - corner1_y);

                /* Validate rectangle */
                if (rect_width == 0 || rect_height == 0) {
                    printf("ERRO: Area selecionada e vazia.\n");
                    break;
                }

                if (rect_x < 0 || rect_y < 0 || 
                    (rect_x + rect_width > IMG_WIDTH) || 
                    (rect_y + rect_height > IMG_HEIGHT)) {
                    printf("ERRO: Coordenadas fora dos limites da imagem (%dx%d).\n",
                           IMG_WIDTH, IMG_HEIGHT);
                    printf("      Area calculada: Inicio(%d,%d) Tamanho[%dx%d]\n", 
                           rect_x, rect_y, rect_width, rect_height);
                    break;
                }

                /* Allocate and read window */
                uint8_t *window_buffer = (uint8_t*)malloc(rect_width * rect_height);
                if (!window_buffer) {
                    printf("ERRO: Memoria insuficiente.\n");
                    break;
                }

                if (read_fpga_window(window_buffer, rect_x, rect_y, 
                                    rect_width, rect_height, mem_sel) == 0) {
                    printf(">>> SUCESSO: Dados lidos da area (%d,%d) a (%d,%d).\n",
                           rect_x, rect_y, rect_x + rect_width, rect_y + rect_height);
                    print_matrix(window_buffer, rect_width, rect_height);
                } else {
                    printf("ERRO: Falha na leitura da FPGA.\n");
                }

                free(window_buffer);
                break;
            }

            /* ==================== ZOOM IN: NEAREST NEIGHBOR ==================== */
            case 4:
            /* ==================== ZOOM IN: PIXEL REPLICATION ==================== */
            case 5: {
                if (!image_sent_to_fpga) {
                    printf("ERRO: Nenhuma imagem na VRAM do FPGA. Carregue uma imagem primeiro (Opcao 1 ou 2).\n");
                    break;
                }

                /* Check zoom level constraints */
                if (zoom_level >= MAX_ZOOM_IN_LEVEL) {
                    printf("ERRO: Nivel maximo de Zoom IN atingido (nivel %d).\n", zoom_level);
                    printf("   Nao e possivel executar mais algoritmos de Zoom IN.\n");
                    printf("   Tente um 'Zoom OUT' (6, 7) ou 'Reset' (8).\n");
                    break;
                }
                
                /* Check hardware flag */
                if (ASM_Get_Flag_Max_Zoom() == 1) {
                    printf("ERRO: A 'Flag de Uso Maximo' (Max_Zoom) esta ATIVA.\n");
                    printf("   Nao e possivel executar mais algoritmos de Zoom IN.\n");
                    printf("   Tente um 'Zoom OUT' (6, 7) ou 'Reset' (8).\n");
                    break;
                }
               
                printf("=== EXECUTANDO ALGORITMO (Zoom IN) ===\n");
                int result;
                if (option == 4) {
                    result = execute_algorithm("NearestNeighbor", &NearestNeighbor);
                } else {
                    result = execute_algorithm("PixelReplication", &PixelReplication);
                }
               
                if (result == 0) {
                    zoom_level++;
                    printf("   Nivel de zoom atual: %d\n", zoom_level);
                } else {
                    printf("ERRO FATAL: Falha na execucao do algoritmo.\n");
                    goto cleanup_error;
                }
                break;
            }

            /* ==================== ZOOM OUT: DECIMATION ==================== */
            case 6:
            /* ==================== ZOOM OUT: BLOCK AVERAGING ==================== */
            case 7: {
                if (!image_sent_to_fpga) {
                    printf("ERRO: Nenhuma imagem na VRAM do FPGA. Carregue uma imagem primeiro (Opcao 1 ou 2).\n");
                    break;
                }

                /* Check zoom level constraints */
                if (zoom_level <= MIN_ZOOM_OUT_LEVEL) {
                    printf("ERRO: Nivel minimo de Zoom OUT atingido (nivel %d).\n", zoom_level);
                    printf("   Nao e possivel executar mais algoritmos de Zoom OUT.\n");
                    printf("   Tente um 'Zoom IN' (4, 5) ou 'Reset' (8).\n");
                    break;
                }
                
                /* Check hardware flag */
                if (ASM_Get_Flag_Min_Zoom() == 1) {
                    printf("ERRO: A 'Flag de Zoom Minimo' (Min_Zoom) esta ATIVA.\n");
                    printf("   Nao e possivel executar mais algoritmos de Zoom OUT.\n");
                    printf("   Tente um 'Zoom IN' (4, 5) ou 'Reset' (8).\n");
                    break;
                }

                printf("=== EXECUTANDO ALGORITMO (Zoom OUT) ===\n");
                int result;
                if (option == 6) {
                    result = execute_algorithm("Decimation", &Decimation);
                } else {
                    result = execute_algorithm("BlockAveraging", &BlockAveraging);
                }
               
                if (result == 0) {
                    zoom_level--;
                    printf("   Nivel de zoom atual: %d\n", zoom_level);
                } else {
                    printf("ERRO FATAL: Falha na execucao do algoritmo.\n");
                    goto cleanup_error;
                }
                break;
            }

            /* ==================== RESET ==================== */
            case 8: {
                printf("=== EXECUTANDO: RESET ===\n");
                ASM_Reset();
                zoom_level = 0;
                ASM_Pulse_Enable();
                usleep(PULSE_DELAY_US);
                printf("   [C] Reset concluido. Flags zeradas.\n");
                printf("   Nivel de zoom resetado para: %d\n", zoom_level);
                break;
            }

            case 9: {
                if (!image_sent_to_fpga) {
                    printf("ERRO: Carregue uma imagem primeiro.\n");
                    break;
                }
                if (mouse_fd_global < 0) {
                    printf("ERRO: Mouse nao inicializado.\n");
                    break;
                }
                if (zoom_level < 0) {
                    printf("ERRO: Zoom Regional nao permitido (zoom_level < 0).\n");
                    break;
                }
                
                RegionalZoomContext regional_ctx;
                if (regional_zoom_start(&regional_ctx) != 0)
                    break;
                
                int regional_loop = 1;
                while (regional_loop) {
                    printf("\n\n=== MENU ZOOM REGIONAL ===\n");
                    printf("JANELA: Pos(%d,%d) Tamanho[%dx%d] ZoomLevel[%d/%d]\n",
                           regional_ctx.x, regional_ctx.y,
                           regional_ctx.width, regional_ctx.height,
                           regional_ctx.zoom_level, MAX_REGIONAL_ZOOM_LEVELS - 1);
                    printf("Controles:\n [+] Zoom IN\n [-] Zoom OUT\n [0] Voltar (ou ESC)\n");
                    printf("Aguardando comando... ");
                    fflush(stdout); // Garante que o texto apareça antes de esperar a tecla
                    
                    // Lê a tecla diretamente
                    char key = read_key_direct();
                    
                    // Processa a tecla
                    switch (key) {
                        case ZOOM_IN:  // '+'
                        case '=':      // Aceita '=' também (tecla + sem shift) para facilitar
                            regional_zoom_apply(&regional_ctx, image_data, zoom_level, ZOOM_IN);
                            break;
                            
                        case ZOOM_OUT: // '-'
                        case '_':      // Aceita '_' também
                            regional_zoom_apply(&regional_ctx, image_data, zoom_level, ZOOM_OUT);
                            break;
                            
                        case '0':
                        case 27:       // Tecla ESC
                            regional_loop = 0;
                            break;
                            
                        default:
                            printf("\nComando invalido: '%c'\n", key);
                            break;
                    }
                    
                    // Se não saiu, dá uma pequena pausa ou espera enter opcionalmente
                    if (regional_loop) {
                         // Opcional: usleep(100000); 
                    }
                }
                // int regional_option;
                // while (1) {
                //     printf("\n\n=== MENU ZOOM REGIONAL ===\n");
                //     printf("JANELA: Pos(%d,%d) Tamanho[%dx%d] ZoomLevel[%d/%d]\n",
                //            regional_ctx.x, regional_ctx.y,
                //            regional_ctx.width, regional_ctx.height,
                //            regional_ctx.zoom_level, MAX_REGIONAL_ZOOM_LEVELS - 1);
                //     printf(" 1. Zoom IN\n 2. Zoom OUT\n 0. Voltar\n: ");
                    
                //     if (scanf("%d", &regional_option) != 1) {
                //         clear_input_buffer();
                //         wait_for_enter();
                //         continue;
                //     }
                    
                //     if (regional_option == 0) break;
                //     if (regional_option == 1)
                //         regional_zoom_apply(&regional_ctx, image_data, zoom_level, ZOOM_IN);
                //     else if (regional_option == 2)
                //         regional_zoom_apply(&regional_ctx, image_data, zoom_level, ZOOM_OUT);
                    
                //     wait_for_enter();
                // }
                
                /* Cleanup buffers before exiting */
                regional_zoom_cleanup(&regional_ctx);
                break;
            }

            /* ==================== EXIT ==================== */
            case 0: {
                printf("=== ENCERRANDO SISTEMA ===\n");
                
                /* Close mouse device if open */
                if (mouse_fd_global >= 0) {
                    close(mouse_fd_global);
                    printf("Mouse fechado.\n");
                }
                
                printf("Encerrando API...\n");
                API_close();
                free(image_data);
                printf("Sistema encerrado com sucesso.\n");
                return 0;
            }

            default:
                printf("ERRO: Opcao invalida. Tente novamente.\n");
                break;
        }

        wait_for_enter();
    }

cleanup_error:
    printf("\n!!! OCORREU UM ERRO FATAL. ENCERRANDO API. !!!\n");
    if (mouse_fd_global >= 0) {
        close(mouse_fd_global);
    }
    API_close();
    free(image_data);
    return -1;
}