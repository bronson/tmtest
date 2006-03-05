// The rel2abs that comes with pathconv is hopeless. It removes .. without
// moving up a directory and the code is pretty bad so it has been purged.
// Just use strcat and then normalize the absolute path.
//
// char *rel2abs(const char *path, const char *base, char *result,
// 		const size_t size);

char *abs2rel(const char *path, const char *base, char *result,
		const size_t size);

int normalize_absolute_path(char *buf);
