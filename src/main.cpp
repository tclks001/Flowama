#include "application.h"

int main(int argumentCount, char* arguments[])
{
    return flowama::RunApplication(argumentCount, arguments) ? 0 : 1;
}
