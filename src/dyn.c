
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>

int main() {
    void *handle;
    void *coseno_addr;
    char *error;

    // 1. Apriamo la libreria dinamica
    handle = dlopen("libm.so.6", RTLD_LAZY);
    if (!handle) {
        fprintf(stderr, "%s\n", dlerror());
        exit(EXIT_FAILURE);
    }

    // Pulizia di eventuali errori precedenti
    dlerror();

    // 2. Cerchiamo l'indirizzo della funzione "cos"
    coseno_addr = dlsym(handle, "cos");

    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(EXIT_FAILURE);
    }

    // 3. Verifica minima: simbolo trovato
    if (!coseno_addr) {
        fprintf(stderr, "Simbolo cos non trovato\n");
        exit(EXIT_FAILURE);
    }
    printf("dlsym(cos) = %p\n", coseno_addr);
    // 2. Cerchiamo l'indirizzo della funzione "cos"
    // dlsym restituisce un void*, quindi facciamo il cast al nostro puntatore
    coseno = (double (*)(double)) dlsym(handle, "cos");

    if ((error = dlerror()) != NULL)  {
        fprintf(stderr, "%s\n", error);
        exit(EXIT_FAILURE);
    }

    // 3. Usiamo la funzione caricata a runtime
    printf("Il coseno di 2.0 è: %f\n", (*coseno)(2.0));
    // 4. Chiudiamo il riferimento alla libreria
    dlclose(handle);

    return 0;
}
