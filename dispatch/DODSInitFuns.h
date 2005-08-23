// DODSInitFuns.h

// 2004 Copyright University Corporation for Atmospheric Research

#ifndef E_DODSInitFuns_h
#define E_DODSInitFuns_h 1

typedef bool (*DODSInitFun)(int argc, char **argv);
typedef bool (*DODSTermFun)(void);

#endif // E_DODSInitFuns_h

// $Log: DODSInitFuns.h,v $
// Revision 1.2  2004/09/09 17:17:12  pwest
// Added copywrite information
//
// Revision 1.1  2004/06/30 20:16:24  pwest
// dods dispatch code, can be used for apache modules or simple cgi script
// invocation or opendap daemon. Built during cedar server development.
//