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
 * Nous devons également déclarer un tableau pout le provisionnement des ressourves
 * */

int *available;
int **max;
int **allocation;
int **need;
int available_resources[]; // sera utilisé pour comparer pour le safe state
                           // et c'est le tableau de quand on appelle BEG
                           // et qu'on le remplis avec PRO

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
    struct sockaddr_in thread_addr;
    socklen_t thread_len = sizeof(thread_addr);
    int newsockfd = accept(server_socket_fd, (struct sockaddr *) &thread_addr, &thread_len);

    if (newsockfd < 0) {
        perror("Error on accept.");
        exit(0);
    }

    /* Exactly the same as in st_process_request*/
    FILE *socket_r = fdopen(newsockfd, "r");
    FILE *socket_w = fdopen(newsockfd, "w");
    int reponse[4]; // même longueur que la commande --> ce qui est retournée au CLIENT

    while(true) {
        char cmd[4] = {NUL, NUL, NUL, NUL};
        if (!fread(cmd, 3, 1, socket_r))
            break;
        char *args = NULL;
        size_t args_len = 0;
        ssize_t cnt = getline(&args, &args_len, socket_r);
        if (!args || cnt < 1 || args[cnt - 1] != '\n') {
            break;
        }
        /* Pour la commande BEG */
        if(cmd[0] == 'B' && cmd[1] == 'E' && cmd[2] == 'G') {
            num_resources = args[0];
            available_resources = malloc(num_resources * sizeof(int));
            reponse[0] = 'A';
            reponse[1] = 'C';
            reponse[2] = 'K';
            break;
        }

        /* Pour la commande PRO */
        if(cmd[0] == 'P' && cmd[1] == 'R' && cmd[2] == 'O') {
            for(int i = 0; i < num_resources; i++) {
                /* PRO r1 r2 r3 r4 r5 --> PRO 10(r1) 5(r2) 3(r3) 23(r4) 1(r5) */
                /* [10, 5, 3, 23, 1] */
                available_resources[i] = args[i];
            }
            reponse[0] = 'A';
            reponse[1] = 'C';
            reponse[2] = 'K';
            break;
        }
    }

    /* Initisalisation de structures de données pour l'algo du banquier */

    available = (int *)malloc(num_resources * sizeof(int));
    max = (int **)malloc(nb_registered_clients * sizeof(int));
    allocation = (int **)malloc(nb_registered_clients * sizeof(int));
    need = (int **)malloc(nb_registered_clients * sizeof(int));

    /* If there are no resources and no clients */
    if (available == NULL || max == NULL || allocation == NULL || need == NULL) {
        perror("No ressources and no clients -- null pointer exception");
    }

    /*
     * Nous n'avons pas fait de double boucle for, car la complexité aurait été
     * de n^2 comparativement à une complexité de 2n
     */

    for (int i = 0; i < num_resources; i++) {
        // tous les i on nb_resources --> par défaut on met à 0
        available[i] = 0;
    }

    for (int j = 0; j < nb_registered_clients; j++) {
        max[j] = (int *)malloc(num_resources * sizeof(int));
        allocation[j] = (int *)malloc(num_resources * sizeof(int));
        need[j] = (int *)malloc(num_resources * sizeof(int));

        /* Manage if there are no resources */
        if (max[j] == NULL || allocation[j] == NULL || need[j] == NULL) {
            perror("No resources -- null pointer exception");
        }

        /*
         * By default, we set max[i][j], allocation [i][j] and need [i][j]
         * to 0 as proces Pi is currently allocated ‘k (which is 0)’ instances of
         * resource type Rj
         * (thank you: https://www.youtube.com/watch?v=T0FXvTHcYi4)
         *
         *      Max       Allocation     Available  Need
         *    A B C D     A B C D       A B C D     Max - Allocation
         * P0 0 0 1 2  P0 0 0 1 2       1 5 2 0
         * P1 1 7 5 0  P1 1 0 0 0
         * P2 2 3 5 6  P2 1 3 5 4
         *
         * After we have to determine the safe state.
         * Available + Allocation = New Available
         */

        for (int i = 0; i < num_resources; i++) {
            max[i][j] = 0; // 'k' = 0
            allocation[i][j] = 0; // 'k' = 0
            need[i][j] = 0; // 'k' = 0
        }
    }

}

/* À utiliser lorsqu'on répond au client */
void response_to_client (int socket_fd, int reponse[4], int len) {
    int n;
    /* Write a response to the client */
    n = (int) write(socket_fd, reponse, (size_t) len);

    if (n < 0) {
        perror("ERROR writing to socket");
        exit(1);
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

        /*
         * Définir un char qui indique la réponse que le serveur envoye au client
         * suite à une requête
         */
        int reponse[4]; // même longueur que la commande

        /* Pour la commande INI */
        if(cmd[0] == 'I' && cmd [1] == 'N' && cmd[2] == 'I') {
            max[st->id] = malloc(num_resources * sizeof(int));

            /*
             * Même principe que plus bas, vérifier que la requête est valide
             * i.e., vérifier que l'usage du client tid ne dépasse pas le max
             * que peut provisionner chaque ressource
             */

            int valid_request = 1;
            for(int i = 0; i < num_resources; i++) {
                valid_request = valid_request && args[1 + i] <= available[i];
                max[st->id][i] = args[1 + i];
            }

            if(valid_request == 1) {
                reponse[0] = 'A';
                reponse[1] = 'C';
                reponse[2] = 'K';
            }
            /* Gérer le cas ou que la requête n'est pas valide ...*/
            break;
        }

        /* Pour la commande END */
        if (cmd[0] == 'E' && cmd[1] == 'N' && cmd[2] == 'D') {
            /* On doit fermer les clients */
            exit(0); // Exit successful
        }

        /* Pour la commande REQ */
        if (cmd[0] == 'R' && cmd[1] == 'E' && cmd[2] == 'Q') { // REQ
            request_processed++;

            /*
             * Algo du banquier, nous devons faire declarer un int
             * pour voir si le REQ est valide => qu'il n'y a pas
             * d'excess de demande de ressources.
             */
            int valid_request = 1; // 1 by default, because we will use as a true/false

            /*
             * La requete est de la forme REQ t_id r1, r2, r3, r4, r5
             * Iterer sur les ri
             * De plus, nous faisons args[1 + i] puisque la position 0 est le t_id
             */
            for (int i = 0; i < num_resources; i++) {
                if (args[1 + i] < 0) { // negative ressource -> desallocation
                    valid_request = valid_request &&
                                    (-args[1 + i] + allocation[args[1]][i] <= max[args[0]][i]);
                } else if (args[1 + i] > 0) {
                    valid_request = valid_request &&
                                    (args[1 + i] + allocation[args[1]][i] <= max[args[0]][i]);
                }
            }

            if (valid_request == 0) { // une requete invalide
                count_invalid++;
                break; // sortir de la boucle while
            }

            /*
             * Puisque la requête est valide, nous devons vérifier s'il y a
             * assez de ressources pour chaque ri, donc comparer avec le
             * INI du t_id respectif pour que ça ne dépasse pas l'usage
             * maximal pour chaque ri du t_id.
             */
            int assez_ressources = 1;
            for (int i = 0; i < num_resources; i++) {
                if (args[1 + i] < 0) {
                    assez_ressources = assez_ressources
                                       && -args[1 + i] <= available[i];
                }
            }

            if (assez_ressources == 0) { // il n'y a pas assez de ressources
                count_wait++;
                reponse[0] = 'W';
                reponse[1] = 'A';
                reponse[2] = 'I';
                reponse[3] = 'T';
                /* => reponse = WAIT */
                break; // sortir de la boucle while
            }

            /* Faire un safe state, pour l'algo du banquier ... ?? how tho ??*/

        }

        /* Pour la commande CLO */
        if (cmd[0] == 'C' && cmd[1] == 'L' && cmd[2] == 'O') { // CLO
            clients_ended++;

            int ended_correctly = 1;
            for(int i = 0; i < num_resources; i++) {
                ended_correctly = ended_correctly
                                  && allocation[args[0]][i] == 0;
                /* S'assurer que le t_id a 0 instances de la ressources ri */
            }

            if(ended_correctly == 1) {
                count_dispatched++;
                /* Le serveur renvoye un ACK -> commande exécutée avec succès*/
                reponse[0] = 'A';
                reponse[1] = 'C';
                reponse[2] = 'K';
            }
            break; // sortir de la boucle while
        }

        printf("Thread %d received the command: %s%s", st->id, cmd, args);

        fprintf(socket_w, "ERR Unknown command\n");
        free(args);
    }

    fclose(socket_r);
    fclose(socket_w);
    // TODO end
}

/*
 * For the Bankers Algorithm to "work" -- if we can -- we have to implement a
 * int isSafeState function to determine if the system is in a safe state
 * or not.
 * Available + Allocation = New Available
 */

int isSafeState() {


}

void
st_signal() {
    // TODO: Remplacer le contenu de cette fonction
    /* Very similar to the initialization of st_init() */

    struct sockaddr_in thread_addr;
    socklen_t thread_len = sizeof(thread_addr);
    int newsockfd = accept(server_socket_fd, (struct sockaddr *) &thread_addr, &thread_len);

    if (newsockfd < 0) {
        perror("Error on accept.");
        exit(0);
    }

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
    server_socket_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
#endif
    if (server_socket_fd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_number);

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
