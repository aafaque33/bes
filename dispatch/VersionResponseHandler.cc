// VersionResponseHandler.cc

// 2004 Copyright University Corporation for Atmospheric Research

#include "VersionResponseHandler.h"
#include "DODSTextInfo.h"
#include "cgi_util.h"
#include "util.h"
#include "dispatch_version.h"
#include "DODSParserException.h"
#include "TheRequestHandlerList.h"
#include "DODSTokenizer.h"

VersionResponseHandler::VersionResponseHandler( string name )
    : DODSResponseHandler( name )
{
}

VersionResponseHandler::~VersionResponseHandler( )
{
}

/** @brief executes the command 'show version;' by returning the version of
 * the OPeNDAP-g server and the version of all registered data request
 * handlers.
 *
 * This response handler knows how to retrieve the version of the OPeNDAP-g
 * server. It adds this information to a DODSTextInfo informational response
 * object. It also forwards the request to all registered data request
 * handlers.
 *
 * @param dhi structure that holds request and response information
 * @throws DODSResponseException if there is a problem building the
 * response object
 * @see _DODSDataHandlerInterface
 * @see DODSTextInfo
 * @see TheRequestHandlerList
 */
void
VersionResponseHandler::execute( DODSDataHandlerInterface &dhi )
{
    DODSTextInfo *info = new DODSTextInfo( dhi.transmit_protocol == "HTTP" ) ;
    _response = info ;
    info->add_data( "Core Libraries\n" ) ;
    info->add_data( (string)"    " + dispatch_version() + "\n" ) ;
    info->add_data( (string)"    " + dap_version() + "\n" ) ;
    info->add_data( "Request Handlers\n" ) ;
    TheRequestHandlerList->execute_all( dhi ) ;
}

/** @brief transmit the response object built by the execute command
 * using the specified transmitter object
 *
 * If a response object was built then transmit it as text.
 *
 * @param transmitter object that knows how to transmit specific basic types
 * @param dhi structure that holds the request and response information
 * @see DODSResponseObject
 * @see DODSTransmitter
 * @see _DODSDataHandlerInterface
 */
void
VersionResponseHandler::transmit( DODSTransmitter *transmitter,
                                  DODSDataHandlerInterface &dhi )
{
    if( _response )
    {
	DODSTextInfo *info = dynamic_cast<DODSTextInfo *>(_response) ;
	transmitter->send_text( *info, dhi );
    }
}

DODSResponseHandler *
VersionResponseHandler::VersionResponseBuilder( string handler_name )
{
    return new VersionResponseHandler( handler_name ) ;
}

// $Log: VersionResponseHandler.cc,v $
// Revision 1.4  2005/03/15 19:58:35  pwest
// using DODSTokenizer to get first and next tokens
//
// Revision 1.3  2005/02/01 17:48:17  pwest
//
// integration of ESG into opendap
//
// Revision 1.2  2004/09/09 17:17:12  pwest
// Added copywrite information
//
// Revision 1.1  2004/06/30 20:16:24  pwest
// dods dispatch code, can be used for apache modules or simple cgi script
// invocation or opendap daemon. Built during cedar server development.
//