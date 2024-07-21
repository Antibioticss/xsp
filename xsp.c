#include "hexpatch/hexpatch.h"

char data[] =
    "1. abc\n"
    "2. def\n"
    "3. abc\n"
    "4. def\n"
    "5. abc\n";
char buffer[100] = {0};

int main() {
    FILE * fp = fopen("test.txt", "w");
    fwrite(data, sizeof data, 1, fp);
    fclose(fp);

    fp = fopen("test.txt", "rb+");
    patch_single(fp, 3, "def", "bbb");
    fclose(fp);

    fp = fopen("test.txt", "r");
    fread(buffer, 1, 100, fp);
    fclose(fp);
    printf("buffer:\n%s\n", buffer);
    return 0;
}