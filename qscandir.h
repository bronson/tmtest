struct dirent;

int qdirentcmp(char **d1, char **d2);
int qdirentcoll(char **d1, char **d2);

char** qscandir(const char *directory_name,
        int (*select_function)(const struct dirent *),
        int (*compare_function) (char **d1, char **d2)
        );

