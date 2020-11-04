#ifndef __FILE_CALLARG__
#define __FILE_CALLARG__
#define RWSTARTOFFSET 256
struct fileclose_arg
{
    int index;
};

struct fileread_arg
{
    int index;
    uint sz;
};

struct filewrite_arg
{
    int index;
    uint sz;
};



#endif