#include <iostream>

int main()
{
    int a=0;
    for(int i=0;i<100;i++)
    {
        std::cout<<"[app] addr: 0x"<<std::hex<<&a
            <<"data: 0x"<<i<<" "<<std::endl;
    }
    return 1;
}
