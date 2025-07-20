#include "url_decode.h"
#include <stdio.h>
#include <string.h>

/* Unit test helpers */
#define COMMENT(x) printf("\n----" x "----\n");
#define TEST(x) \
    if (!(x)) { \
        fprintf(stderr, "\033[31;1mFAILED:\033[22;39m %s:%d %s\n", __FILE__, __LINE__, #x); \
        status = false; \
    } else { \
        printf("\033[32;1mOK:\033[22;39m %s\n", #x); \
    }

bool test_basics()
{
    bool status = true;
    char value[] = "Game+%28USA%2C+Europe%29";

    COMMENT("test_basics()");
    urldecode(value);
    TEST(strcmp(value, "Game (USA, Europe)") == 0);
    return status;
}


bool test_case_insensitive()
{
    bool status = true;
    char value[] = "%2c%2C";

    COMMENT("test_case_insensitive()");
    urldecode(value);
    TEST(strcmp(value, ",,") == 0);
    return status;
}


bool test_invalid_escape()
{
    bool status = true;
    char value[] = "%%28%2G%g2%29%";

    COMMENT("test_invalid_escape()");
    urldecode(value);
    TEST(strcmp(value, "%(%2G%g2)%") == 0);
    return status;
}


bool test_no_escape()
{
    bool status = true;
    char value[] = "Game.iso";

    COMMENT("test_no_escape()");
    urldecode(value);
    TEST(strcmp(value, "Game.iso") == 0);
    return status;
}


int main()
{
    if (test_basics() && test_case_insensitive() && test_invalid_escape() && test_no_escape())
    {
        return 0;
    }
    else
    {
        printf("Some tests failed\n");
        return 1;
    }
}
