#include <stdio.h>

int main(void)
{
    printf("Content-Type: text/plain\r\n\r\n");
    printf("This CGI fails intentionally.\n");
    fflush(stdout);
    return 42;
}
