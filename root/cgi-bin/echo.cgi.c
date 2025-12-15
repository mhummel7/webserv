#include <stdio.h>
#include <stdlib.h>

int main(void)
{
    char *len_str = getenv("CONTENT_LENGTH");
    int len = len_str ? atoi(len_str) : 0;

    printf("Content-Type: text/plain\r\n\r\n");

    printf("CONTENT_LENGTH=%d\n", len);
    printf("BODY:\n");

    for (int i = 0; i < len; i++)
    {
        int c = getchar();
        if (c == EOF)
            break;
        putchar(c);
    }
    putchar('\n');

    return 0;
}
