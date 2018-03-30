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
#include <stddef.h>


enum {
    NUL = '\0'
};

enum {
    /* Configuration constants.  */
            max_wait_time = 30,
    server_backlog_size = 5
};

unsigned int server_socket_fd;

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

/* Nombre de ressources lorsqu'on lance le BEG */
int num_resources;

/* Nombre de client enregistré. */
int nb_registered_clients;

/*
 * Définir un char qui indique la réponse que le serveur envoye au client
 * suite à une requête
 */
char reponse[4] = {NUL, NUL, NUL, NUL}; // même longueur que la commande

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

    /* Appeler st_process_request() --> pour les PRO et BEG */
    //st_process_requests();

    /* Initisalisation de structures de données pour l'algo du banquier */

    available = (int *)malloc(num_resources * sizeof(int));
    max = (int **)malloc(nb_registered_clients * sizeof(int));
    allocation = (int **)malloc(nb_registered_clients * sizeof(int));
    need = (int **)malloc(nb_registered_clients * sizeof(int));

    /* If there are no resources and no clients */
    if (available == NULL || max == NULL || allocation == NULL || need == NULL) {
        perror("No ressources and no clients -- null pointer exception");
    }

    for (int i = 0; i < num_resources; i++) {
        // tous les i on nb_resources --> par défaut on met à 0
        // maybe change this ?
        available[i] = 0;
    }

    for (int i = 0; i < nb_registered_clients; i++) {
        max[i] = (int *)malloc(num_resources * sizeof(int));
        allocation[i] = (int *)malloc(num_resources * sizeof(int));
        need[i] = (int *)malloc(num_resources * sizeof(int));

        /* Manage if there are no resources */
        if (max[i] == NULL || allocation[i] == NULL || need[i] == NULL) {
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

        for (int j = 0; j < num_resources; j++) {
            max[i][j] = 0; // 'k' = 0
            allocation[i][j] = 0; // 'k' = 0
            need[i][j] = 0; // 'k' = 0
        }
    }
    /* -- ? Envoyer au client ? --*/
    //response_to_client();

}

/* Nous allons maintenant implémenter le Bankers Algorithm. */

/*
 * Fonction pour comparer 2 array de int
 * Notez que cette fonction est pour comparer les instances des ressources
 * donc dans notre cas, array_a[i] doit toujours être <= array_b[i].
 */
int compare_array(int *array_a, int *array_b) {

    int v1 = sizeof(array_a)/ sizeof(int);
    int v2 = sizeof(array_b)/ sizeof(int);

    /* Premièrement, comparer leurs tailles */
    if(v1 != v2) {
        return -1;
    }
    for(int i = 0; i < num_resources; i++){
        if(array_a[i] > array_b[i]) {
            return 0; // false pas correct
        }
    }
    /*
     * Tout est beau, les valeurs des instances de ressources sont <= au "max"
     * on peut retoutner 1
     */
    return 1;
}

// This function tells us if the system is safe or unsafe.
// 1 -> safe, 0 -> unsafe
// Inspiration de https://www.studytonight.com/operating-system/bankers-algorithm

int safe_state_banker () {

    int finish[nb_registered_clients];
    int work[num_resources];

    for(int i = 0; i < num_resources; i++) {
        work[i] = available[i];
    }

    for(int i = 0; i < nb_registered_clients; i++) {
        finish[i] = 0;
    }

    for (int i = 0; i < nb_registered_clients; i++){
        if(finish[i] == 0 && compare_array(need[i], work)) {
            work[i] = work[i] + *allocation[i] ;
            finish[i] = 1; // finish[i] = true
        }
    }
    for (int i = 0; i < nb_registered_clients; i++) {
        if (finish[i] == 0) {
            return 0; // not safe state
        }
    }
    /* SAFE STATE xD */
    return 1;
}

/* Resource Request Algorithm
 * request --> la requête avec les ressources
 * req --> la requête
 * 1 -> requête acceptée, 0 -> requête refusée
 * request -> 1 0 10 4 3
 * req 1 (r1) ou 2 (r2) ...
 */
int resource_request_algorithm (int *request, int req) {

    for (int i = 0; i < num_resources; i++) {

        /* 1. */
        if(compare_array(request, need[req]) != 1) {
            perror("We gonna have to make you wait my dude! -feelsbad-");
        }

        /* 2. */
        while (compare_array(request, need[req]) != 1) {
            /* Attend -_- */
        }

        /* 3. -> we assume that the ressources have been allocated. */
        for(int i = 0; i < num_resources; i++) {
            available[i] = available[i] - request[i];
            allocation[i] = allocation[i] + request[i];
            need[i] = need[i] - request[i];
        }
        if(!safe_state_banker()) {
            for(int i = 0; i < num_resources; i++) {
                available[i] = available[i] + request[i];
                allocation[i] = allocation[i] - request[i];
                need[i] = need[i] + request[i];
            }
            return 0;
        }
        return 1;
    }
}

/*
 * SOURCE : https://stackoverflow.com/questions/8512958/is-there-a-windows-variant-of-strsep
 * Nous n'avons pas codé le mystrsep.
 */
char* mystrsep(char** stringp, const char* delim)
{
    char* start = *stringp;
    char* p;
    p = (start != NULL) ? strpbrk(start, delim) : NULL;
    if (p == NULL)
    {
        *stringp = NULL;
    }
    else
    {
        *p = '\0';
        *stringp = p + 1;
    }
    return start;
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

/* Ici nous implémentons les différents protocoles de communication */

void st_process_requests_BEG(char *cmd, char *args) {

    /* Nous devons séparer le args en tokens --> i.e.: 12 10 */
    strstr(args, " ");
    char *token = strtok(args, " ");
    char *args_1 = malloc((strlen(token)+1) * sizeof(char));
    char *args_2 = malloc((strlen(token)+1) * sizeof(char));
    strcpy(args_1, token);
    token = strtok(NULL, " ");
    strcpy(args_2, token);

    num_resources = atoi(args_1);
    nb_registered_clients = atoi(args_2);
    reponse[0] = 'A';
    reponse[1] = 'C';
    reponse[2] = 'K';
    /* Free at last */
    free(args_1);
    free(args_2);
}

void st_process_requests_PRO(char *cmd, char *args){

    /*
     * Il faut prendre la commande de PRO et ses arguments et les
     * passer à chaque colonnes de available
     */
    char *running;
    char *token;
    running = strdup(args);

    for(int i = 0; i < num_resources; i++){
        token = mystrsep(&running, " ");
        available[i] = atoi(token);
    }
    reponse[0] = 'A';
    reponse[1] = 'C';
    reponse[2] = 'K';
}

/*
 * On a besoin du server_thread puisque chaque thread a un ini a lui-seul.
 * INI args1, args2, args3, ..., argsn -> les args sont l'usage maximal que
 * le process i peut utiliser.
 */
void st_process_requests_INI (char *cmd, char *args, server_thread *st) {

    max[st->id] = malloc(num_resources * sizeof(int));

    /*
     * Même principe que plus bas, vérifier que la requête est valide
     * i.e., vérifier que l'usage du client tid ne dépasse pas le max
     * que peut provisionner chaque ressource
     */

    int valid_request = 1;
    for(int i = 0; i < num_resources; i++) {
        valid_request = valid_request && args[i] <= available[i];
        max[st->id][i] = args[i];
    }

    if(valid_request == 1) {
        reponse[0] = 'A';
        reponse[1] = 'C';
        reponse[2] = 'K';
    }
    /* Gérer le cas ou que la requête n'est pas valide ...*/
}

void st_process_requests_END() {

    /* On doit fermer les clients et nous devons free toutes les structures. */
    free(available);
    free(max);
    free(allocation);
    free(need);
    exit(0); // Exit successful
}

void st_process_requests_REQ(char *cmd, char *args, server_thread *st) {

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
                            (-args[i] + allocation[args[st->id]][i] <= max[args[st->id]][i]);
        } else if (args[1 + i] > 0) {
            valid_request = valid_request &&
                            (args[i] + allocation[args[st->id]][i] <= max[args[st->id]][i]);
        }
    }

    if (valid_request == 0) { // une requete invalide
        count_invalid++;
        reponse[0] = 'E';
        reponse[1] = 'R';
        reponse[2] = 'R';
        perror("-- Requête invalide --");
        /* Renvoyer un ERR */
        return;
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
                               && -args[i] <= available[i];
        }
    }

    if (assez_ressources == 0) { // il n'y a pas assez de ressources
        count_wait++;
        reponse[0] = 'W';
        reponse[1] = 'A';
        reponse[2] = 'I';
        reponse[3] = 'T';
        /* => reponse = WAIT */
        return;
    }

    int safe_state;
    for(int i = 0; i <num_resources; i++) {
        /* TODO: Arranger ceci ou si possible tester -> car ne fait pas vraiment de sens */
        safe_state = resource_request_algorithm (need[i], available[i]);
        if (safe_state == 0) {
            /* Unsafe state -- on doit attendre */
            count_wait++;
            reponse[0] = 'W';
            reponse[1] = 'A';
            reponse[2] = 'I';
            reponse[3] = 'T';
            return; // on doit sortir.
        }
    }
    /* ! We are safe ! */
    reponse[0] = 'A';
    reponse[1] = 'C';
    reponse[2] = 'K';
}

void st_process_requests_CLO (char *cmd, char *args, server_thread *st) {

    clients_ended++;

    int ended_correctly = 1;
    for(int i = 0; i < num_resources; i++) {
        ended_correctly = ended_correctly
                          && allocation[args[st->id]][i] == 0;
        /* S'assurer que le t_id a 0 instances de la ressources ri */
    }

    if(ended_correctly == 1) {
        count_dispatched++;
        /* Le serveur renvoye un ACK -> commande exécutée avec succès*/
        reponse[0] = 'A';
        reponse[1] = 'C';
        reponse[2] = 'K';
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

        /* Pour la commande BEG */
        /* BEG est de la forme BEG _nbRessources_ _nbClients_*/
        if(cmd[0] == 'B' && cmd[1] == 'E' && cmd[2] == 'G') {
            st_process_requests_BEG(cmd, args);
            break;
        }

        /* Pour la commande PRO */
        /* PRO est de la forme PRO nb(r1) nb(r2) nb(r3) nb(r4) nb(r5) */
        if(cmd[0] == 'P' && cmd[1] == 'R' && cmd[2] == 'O') {
            st_process_requests_PRO(cmd, args);
            break;
        }

        /* Pour la commande INI */
        if(cmd[0] == 'I' && cmd [1] == 'N' && cmd[2] == 'I') {
            st_process_requests_INI(cmd, args, st);
            break;
        }

        /* Pour la commande END */
        if (cmd[0] == 'E' && cmd[1] == 'N' && cmd[2] == 'D') {
            st_process_requests_END();
        }

        /* Pour la commande REQ */
        if (cmd[0] == 'R' && cmd[1] == 'E' && cmd[2] == 'Q') { // REQ
            st_process_requests_REQ(cmd, args, st);
            break;
        }

        /* Pour la commande CLO */
        if (cmd[0] == 'C' && cmd[1] == 'L' && cmd[2] == 'O') { // CLO
            st_process_requests_CLO(cmd, args, st);
            break;
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
