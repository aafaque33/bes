// SSLClient.h

// This file is part of bes, A C++ back-end server implementation framework
// for the OPeNDAP Data Access Protocol.

// Copyright (c) 2004,2005 University Corporation for Atmospheric Research
// Author: Patrick West <pwest@ucar.org> and Jose Garcia <jgarcia@ucar.org>
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// 
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
// 
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// You can contact University Corporation for Atmospheric Research at
// 3080 Center Green Drive, Boulder, CO 80301
 
// (c) COPYRIGHT University Corporation for Atmostpheric Research 2004-2005
// Please read the full copyright statement in the file COPYRIGHT_UCAR.
//
// Authors:
//      pwest       Patrick West <pwest@ucar.edu>
//      jgarcia     Jose Garcia <jgarcia@ucar.edu>

#ifndef SSLClient_h
#define SSLClient_hS 1

#include <openssl/ssl.h>

#include <string>

using std::string ;

#include "SSLConnection.h"

class Socket ;

class SSLClient : public SSLConnection
{
private:
    string			_host ;
    int				_port ;
    string			_cfile ;
    string			_kfile ;

    int				connect_to_server( ) ;
    int				verify_connection( ) ;

    static int			verify_server( int ok, X509_STORE_CTX *ctx ) ;
public:
    				SSLClient( const string &hostStr, int portVal,
					   const string &cert_file,
					   const string &key_file );
    				~SSLClient() ;
    virtual void		initConnection() ;
} ;

#endif // SSLClient_h

