#define _XOPEN_SOURCE 700   /* So as to allow use of `fdopen` and `getline`.  */

#include "server_thread.h"

#include <netinet/in.h>
#include <netdb.h>

#include <strings.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>

#include <signal.h>

#include <time.h>

/* Add the protocol.h */
#include "../protocol.h"

enum {
    NUL = '\0'
};

enum {
    /* Configuration constants.  */
            max_wait_time = 30,
    server_backlog_size = 5
};

unsigned int server_socket_fd;

// Nombre de client enregistré.
int nb_registered_clients;

// Variable du journal.
// Nombre de requêtes acceptées immédiatement (ACK envoyé en réponse à REQ).
unsigned int count_accepted = 0;

// Nombre de requêtes acceptées après un délai (ACK après REQ, mais retardé).
unsigned int count_wait = 0;

// Nombre de requête erronées (ERR envoyé en réponse à REQ).
unsigned int count_invalid = 0;

// Nombre de clients qui se sont terminés correctement
// (ACK envoyé en réponse à CLO).
unsigned int count_dispatched = 0;

// Nombre total de requête (REQ) traités.
unsigned int request_processed = 0;

// Nombre de clients ayant envoyé le message CLO.
unsigned int clients_ended = 0;

// TODO: Ajouter vos structures de données partagées, ici.
/*
 * Initialisation of variables for the Banker's Algorithm
 * Inspiration de https: https://www.geeksforgeeks.org/program-bankers-algorithm-set-1-safety-algorithm/
 *
 * Notez que available est un "tableau de 1d" --> m resouces
 * et que max, allocation et need sont des "tableaux de 2d" --> n process and m resources
 *
 * */
int *available;
int **max;
int **allocation;
int **need;

static void sigint_handler(int signum) {
    // Code terminaison.
    accepting_connections = 0;
}

void
st_init() {
    // Handle interrupt
    signal(SIGINT, &sigint_handler);

    // Initialise le nombre de clients connecté.
    nb_registered_clients = 0;

    // Attend la connection d'un client et initialise les structures pour
    // l'algorithme du banquier.

    /* Initialiser le server */
    struct sockaddr_in cli_addr;
    int clilen = sizeof(cli_addr);
    int newsockfd = accept(server_socket_fd, (struct sockaddr *) &cli_addr, &clilen);

    if (newsockfd < 0) {
        perror("Error on accept.");
        exit(0);
    }

    /* Initisalisation de structures de données pour l'algo du banquier */

    available = malloc(nb_resources * sizeof(int));
    max = malloc(nb_registered_clients * sizeof(int));
    allocation = malloc(nb_registered_clients * sizeof(int));
    need = malloc(nb_registered_clients * sizeof(int));

    /* If there are no resources and no clients */
    if (available == NULL || max == NULL || allocation == NULL || need == NULL) {
        perror("No ressources and no clients -- null pointer exception");
    }

    /*
     * Nous n'avons pas fait de double boucle for, car la complexité aurait été
     * de n^2 comparativement à une complexité de 2n
     */

    for (int i = 0; i < nb_resources; i++) {
        available[i] = nb_resources; // tous les i on nb_resources
    }

    for (int j = 0; j < nb_registered_clients; j++) {
        max[j] = malloc(nb_resources * sizeof(int));
        allocation[j] = malloc(nb_resources * sizeof(int));
        need[j] = malloc(nb_resources * sizeof(int));

        /* Manage if there are no resources */
        if (max[j] == NULL || allocation[j] == NULL || need[j] == NULL) {
            perror("No resources -- null pointer exception");
        }

        /*
         * By default, we set max[i][j], allocation [i][j] and need [i][j]
         * to 0 as proces Pi is currently allocated ‘k (which is 0)’ instances of
         * resource type Rj
         */

        for (int i = 0; i < nb_resources; i++) {
            max[i][j] = 0; // 'k' = 0
            allocation[i][j] = 0; // 'k' = 0
            need[i][j] = 0; // 'k' = 0
        }
    }

}

void
st_process_requests(server_thread *st, int socket_fd) {
    // TODO: Remplacer le contenu de cette fonction
    FILE *socket_r = fdopen(socket_fd, "r");
    FILE *socket_w = fdopen(socket_fd, "w");

    while (true) {
        char cmd[4] = {NUL, NUL, NUL, NUL};
        if (!fread(cmd, 3, 1, socket_r))
            break;
        char *args = NULL;
        size_t args_len = 0;
        ssize_t cnt = getline(&args, &args_len, socket_r);
        if (!args || cnt < 1 || args[cnt - 1] != '\n') {
            printf("Thread %d received incomplete cmd=%s!\n", st->id, cmd);
            break;
        }

        /* Pour la commande END 2*/
        if (cmd[0] == 'E' && cmd[1] == 'N' && cmd[2] == 'D') {
            /* Appeler st_signal pour fermer les threads et free toutes les ressources */
            st_signal();
            exit(0); // Exit successful
        }

        /* Pour la commande ACK */
        if(cmd[0] == 'A' && cmd[1] == 'C' && cmd[2] == 'K') {

        }

        /* Pour la commande ERR */
        if(cmd[0] == 'E' && cmd[1] == 'R' && cmd[2] == 'R') {

        }

        /* Pour la commande WAIT */
        if(cmd[0] == 'W' && cmd[1] == 'A' && cmd[2] == 'I' && cmd[3] == 'T') {

        }

        printf("Thread %d received the command: %s%s", st->id, cmd, args);

        fprintf(socket_w, "ERR Unknown command\n");
        free(args);
    }

    fclose(socket_r);
    fclose(socket_w);
    // TODO end
}


void
st_signal() {
    // TODO: Remplacer le contenu de cette fonction



    // TODO end
}

int st_wait() {
    struct sockaddr_in thread_addr;
    socklen_t socket_len = sizeof(thread_addr);
    int thread_socket_fd = -1;
    int end_time = time(NULL) + max_wait_time;

    while (thread_socket_fd < 0 && accepting_connections) {
        thread_socket_fd = accept(server_socket_fd,
                                  (struct sockaddr *) &thread_addr,
                                  &socket_len);
        if (time(NULL) >= end_time) {
            break;
        }
    }
    return thread_socket_fd;
}

void *
st_code(void *param) {
    server_thread *st = (server_thread *) param;

    int thread_socket_fd = -1;

    // Boucle de traitement des requêtes.
    while (accepting_connections) {
        // Wait for a I/O socket.
        thread_socket_fd = st_wait();
        if (thread_socket_fd < 0) {
            fprintf(stderr, "Time out on thread %d.\n", st->id);
            continue;
        }

        if (thread_socket_fd > 0) {
            st_process_requests(st, thread_socket_fd);
            close(thread_socket_fd);
        }
    }
    return NULL;
}


//
// Ouvre un socket pour le serveur.
//
void
st_open_socket(int port_number) {
#ifndef SOCK_NONBLOCK
    server_socket_fd = socket(AF_INET, SOCK_STREAM, 0);
#else
    server_socket_fd = socket (AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#endif
    if (server_socket_fd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    memset (&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons (port_number);

    if (bind
                (server_socket_fd, (struct sockaddr *) &serv_addr,
                 sizeof(serv_addr)) < 0)
        perror("ERROR on binding");

    listen(server_socket_fd, server_backlog_size);
}


//
// Affiche les données recueillies lors de l'exécution du
// serveur.
// La branche else ne doit PAS être modifiée.
//
void
st_print_results(FILE *fd, bool verbose) {
    if (fd == NULL) fd = stdout;
    if (verbose) {
        fprintf(fd, "\n---- Résultat du serveur ----\n");
        fprintf(fd, "Requêtes acceptées: %d\n", count_accepted);
        fprintf(fd, "Requêtes : %d\n", count_wait);
        fprintf(fd, "Requêtes invalides: %d\n", count_invalid);
        fprintf(fd, "Clients : %d\n", count_dispatched);
        fprintf(fd, "Requêtes traitées: %d\n", request_processed);
    } else {
        fprintf(fd, "%d %d %d %d %d\n", count_accepted, count_wait,
                count_invalid, count_dispatched, request_processed);
    }
}
