// keysT.C

#include <stdlib.h>
#include <iostream>

using std::cerr ;
using std::cout ;
using std::endl ;

#include "keysT.h"
#include "DODSKeys.h"
#include "DODSException.h"

int keysT::
run(void)
{
    cout << endl << "*****************************************" << endl;
    cout << "Entered keysT::run" << endl;
    int retVal = 0;
    DODSKeys *keys = 0 ;

    cout << endl << "*****************************************" << endl;
    cout << "no file set" << endl;
    putenv( "DODS_INI=" ) ;
    try
    {
	keys = new DODSKeys ;
	cerr << "created, should have not been created" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to create DODSKeys, good, because:" << endl ;
	cout << e.get_error_description() << endl ;
    }
    keys = 0 ;

    cout << endl << "*****************************************" << endl;
    cout << "notfound file set" << endl;
    putenv( "DODS_INI=notfound.ini" ) ;
    try
    {
	keys = new DODSKeys ;
	cerr << "created, should have not been created" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to create DODSKeys, good, because:" << endl ;
	cout << e.get_error_description() << endl ;
    }
    keys = 0 ;

    cout << endl << "*****************************************" << endl;
    cout << "bad keys, not enough equal signs" << endl;
    char *pwd = getenv( "PWD" ) ;
    string pwd_s ;
    if( !pwd )
	pwd_s = "." ;
    else
	pwd_s = pwd ;
    string env_s = "DODS_INI=" + pwd_s + "/bad_keys1.ini" ;
    char env1[1024] ;
    sprintf( env1, "%s", env_s.c_str() ) ;
    putenv( env1 ) ;
    try
    {
	keys = new DODSKeys ;
	cerr << "created, should have not been created" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to create DODSKeys, good, because:" << endl ;
	cout << e.get_error_description() << endl ;
    }
    keys = 0 ;

    cout << endl << "*****************************************" << endl;
    cout << "bad keys, too many equal signs" << endl;
    env_s = "DODS_INI=" + pwd_s + "/bad_keys2.ini" ;
    char env2[1024] ;
    sprintf( env2, "%s", env_s.c_str() ) ;
    putenv( env2 ) ;
    try
    {
	keys = new DODSKeys ;
	cerr << "created, should have not been created" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to create DODSKeys, good, because:" << endl ;
	cout << e.get_error_description() << endl ;
    }
    keys = 0 ;

    cout << endl << "*****************************************" << endl;
    cout << "good keys file, should load" << endl;
    env_s = "DODS_INI=" + pwd_s + "/keys_test.ini" ;
    char env3[1024] ;
    sprintf( env3, "%s", env_s.c_str() ) ;
    putenv( env3 ) ;
    try
    {
	keys = new DODSKeys ;
	cout << "created, good" << endl ;
    }
    catch( DODSException &e )
    {
	cerr << "unable to create DODSKeys, because:" << endl ;
	cerr << e.get_error_description() << endl ;
	return 1 ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "get keys" << endl;
    bool found = false ;
    string ret = "" ;
    for( int i = 1; i < 4; i++ )
    {
	char key[32] ;
	sprintf( key, "DODS.KEY%d", i ) ;
	char val[32] ;
	sprintf( val, "val%d", i ) ;
	cout << "looking for " << key << endl ;
	ret = "" ;
	ret = keys->get_key( key, found ) ;
	if( found == false )
	{
	    cerr << key << " not found" << endl ;
	    return 1 ;
	}
	if( ret == "" )
	{
	    cerr << key << " set to \"\"" << endl ;
	    return 1 ;
	}
	if( ret != val )
	{
	    cerr << key << " = " << ret << ", but should = " << val << endl ;
	    return 1 ;
	}
	else
	{
	    cout << key << " = " << ret << endl ;
	}
    }

    cout << endl << "*****************************************" << endl;
    cout << "look for non existant key" << endl;
    ret = keys->get_key( "DODS.NOTFOUND", found ) ;
    if( found == true )
    {
	cerr << "found DODS.NOTFOUND = \"" << ret << "\"" << endl ;
	return 1 ;
    }
    else
    {
	cout << "did not find DODS.NOTFOUND" << endl ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "look for key with empty value" << endl;
    ret = keys->get_key( "DODS.KEY4", found ) ;
    if( found == true )
    {
	if( ret == "" )
	{
	    cout << "found and is empty" << endl ;
	}
	else
	{
	    cerr << "found DODS.NOTFOUND = \"" << ret << "\"" << endl ;
	    return 1 ;
	}
    }
    else
    {
	cerr << "did not find DODS.KEY4" << endl ;
	return 1 ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "set bad key, 0 = characters" << endl;
    try
    {
	ret = keys->set_key( "DODS.NOEQS" ) ;
	cerr << "set_key successful with value \"" << ret << "\"" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to set the key, good, because:" << endl ;
	cout << e.get_error_description() ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "set bad key, 2 = characters" << endl;
    try
    {
	ret = keys->set_key( "DODS.2EQS=val1=val2" ) ;
	cerr << "set_key successful with value \"" << ret << "\"" << endl ;
	return 1 ;
    }
    catch( DODSException &e )
    {
	cout << "unable to set the key, good, because:" << endl ;
	cout << e.get_error_description() ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "set DODS.KEY5 to val5" << endl;
    try
    {
	ret = keys->set_key( "DODS.KEY5=val5" ) ;
	if( ret == "val5" )
	{
	    cout << "set_key successful" << endl ;
	}
	else
	{
	    cerr << "set successfully, but incorrect val = \""
		 << ret << "\"" << endl ;
	    return 1 ;
	}
    }
    catch( DODSException &e )
    {
	cerr << "unable to set the key, because:" << endl ;
	cerr << e.get_error_description() ;
	return 1 ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "set DODS.KEY6 to val6" << endl;
    try
    {
	ret = keys->set_key( "DODS.KEY6", "val6" ) ;
	if( ret == "val6" )
	{
	    cout << "set_key successful" << endl ;
	}
	else
	{
	    cerr << "set successfully, but incorrect val = \""
		 << ret << "\"" << endl ;
	    return 1 ;
	}
    }
    catch( DODSException &e )
    {
	cerr << "unable to set the key, because:" << endl ;
	cerr << e.get_error_description() ;
	return 1 ;
    }

    cout << endl << "*****************************************" << endl;
    cout << "get keys" << endl;
    for( int i = 1; i < 7; i++ )
    {
	char key[32] ;
	sprintf( key, "DODS.KEY%d", i ) ;
	char val[32] ;
	if( i == 4 ) sprintf( val, "" ) ;
	else sprintf( val, "val%d", i ) ;
	cout << "looking for " << key << endl ;
	ret = "" ;
	ret = keys->get_key( key, found ) ;
	if( found == false )
	{
	    cerr << key << " not found" << endl ;
	    return 1 ;
	}
	if( ret != val )
	{
	    cerr << key << " = " << ret << ", but should = " << val << endl ;
	    return 1 ;
	}
	else
	{
	    cout << key << " = " << ret << endl ;
	}
    }

    cout << endl << "*****************************************" << endl;
    cout << "Returning from keysT::run" << endl;

    return retVal;
}

int
main(int argC, char **argV) {
    Application *app = new keysT();
    return app->main(argC, argV);
}
