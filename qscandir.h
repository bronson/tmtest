struct dirent;

int qalphasort(struct dirent **d1, struct dirent **d2);

int qscandir(const char *directory_name,
        struct dirent ***array_pointer,
        int (*select_function)(const struct dirent *),
        int (*compare_function) (struct dirent **d1, struct dirent **d2)
        );

