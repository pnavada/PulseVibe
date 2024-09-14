#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_NO_HOSTS 10

int main(int argc, char *argv[]) {

    char *file = argv[1];
    char *hosts[MAX_NO_HOSTS];
    FILE *fp;
    char *line = NULL;
    size_t line_size, n_chars;
    int i = 0;

    fp = fopen(file, "r");

    while ((n_chars = getline(&line, &line_size, fp)) != -1) {
        fprintf(stderr, "Host name: %s", line);
        line[line_size-1] = '\0';
        hosts[i] = line;
        i += 1;
    }

    fclose(fp);

    if (line)
        free(line);

    return 0;

}