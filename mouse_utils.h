#ifndef MOUSE_UTILS_H
#define MOUSE_UTILS_H

#include <linux/input.h>
#include <dirent.h>

/*
 * --- MACROS DE UTILITIES PARA TESTE DE BITS ---
 */
#define BITS_PER_LONG (sizeof(long) * 8)
#define NBITS(x) ((((x)-1)/BITS_PER_LONG)+1)

#define OFF(x)  ((x)%BITS_PER_LONG)
#define BIT(x)  (1UL<<OFF(x))
#define LONG(x) ((x)/BITS_PER_LONG)
#define test_bit(bit, array)    ((array[LONG(bit)] >> OFF(bit)) & 1)


/*
 * --- ESTRUTURAS ---
 */

/**
 * @brief Estrutura para manter o estado do "cursor virtual".
 */
typedef struct {
    int x;
    int y;
} Cursor;

/**
 * @brief Estrutura para retornar o estado de um evento.
 */
typedef struct {
    int event_type; // EV_KEY, EV_REL, etc.
    int event_code; // BTN_LEFT, REL_X, etc.
    int event_value;
    Cursor cursor_pos; // Posição virtual atualizada (apenas para EV_REL)
} MouseEvent;


/*
 * --- DECLARAÇÕES DE FUNÇÕES PÚBLICAS ---
 */

/**
 * @brief Verifica se um descritor de arquivo de evento é um mouse.
 * @param fd Descritor de arquivo aberto.
 * @return int 1 se for mouse (suporta REL_X e BTN_LEFT), 0 caso contrário.
 */
int is_mouse(int fd);

/**
 * @brief Procura o primeiro dispositivo de mouse e o abre para leitura.
 * * @param device_path_out Buffer para armazenar o caminho completo do dispositivo (/dev/input/eventX).
 * @param name_out Buffer para armazenar o nome do dispositivo.
 * @return int O descritor de arquivo (fd) do mouse, ou -1 em caso de erro.
 */
int find_and_open_mouse(char *device_path_out, char *name_out);

/**
 * @brief Lê um evento do mouse e o processa para atualizar a posição do cursor.
 * * @param mouse_fd O descritor de arquivo do mouse (obtido por find_and_open_mouse).
 * @param current_cursor Um ponteiro para a estrutura Cursor para atualizar a posição.
 * @param event_out Ponteiro para a estrutura MouseEvent onde os dados serão armazenados.
 * @return int 1 se um evento válido foi lido, 0 se o loop deve parar (EOF ou erro), ou -1 se a leitura falhou.
 */
int read_and_process_mouse_event(int mouse_fd, Cursor *current_cursor, MouseEvent *event_out);

#endif /* MOUSE_UTILS_H */
