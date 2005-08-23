// DODSRequestHandler.cc

// 2004 Copyright University Corporation for Atmospheric Research

#include "DODSRequestHandler.h"

/** @brief add a handler method to the request handler that knows how to fill
 * in a specific response object
 *
 * Add a handler method for a specific response object to the request handler.
 * The handler method takes a reference to a DODSDataHandlerInterface and
 * returns bool, true if the response object is filled in successfully by the
 * method, false otherwise.
 *
 * @param handler_name name of the response object this method can fill in
 * @param handler_method a function pointer to the method that can fill in the
 * specified response object
 * @return true if the handler is added, false if it already exists
 * @see DODSResponseObject
 * @see DODSResponseNames
 */
bool
DODSRequestHandler::add_handler( string handler_name,
			      p_request_handler handler_method )
{
    if( find_handler( handler_name ) == 0 )
    {
	_handler_list[handler_name] = handler_method ;
	return true ;
    }
    return false ;
}

/** @brief remove the specified handler method from this request handler
 *
 * @param handler_name name of the method to be removed, same as the name of
 * the response object
 * @return true if successfully removed, false if not found
 * @see DODSResponseNames
 */
bool
DODSRequestHandler::remove_handler( string handler_name )
{
    DODSRequestHandler::Handler_iter i ;
    i = _handler_list.find( handler_name ) ;
    if( i != _handler_list.end() )
    {
	_handler_list.erase( i ) ;
	return true ;
    }
    return false ;
}

/** @brief find the method that can handle the specified response object type
 *
 * Find the method that can handle the specified response object type. The
 * response object type is the same as the handler name.
 *
 * @param handler_name name of the method that can fill in the response object type 
 * @return the method that can fill in the specified response object type
 * @see DODSResponseObject
 * @see DODSResponseNames
 */
p_request_handler
DODSRequestHandler::find_handler( string handler_name )
{
    DODSRequestHandler::Handler_citer i ;
    i = _handler_list.find( handler_name ) ;
    if( i != _handler_list.end() )
    {
	return (*i).second;
    }
    return 0 ;
}

/** @brief return a comma separated list of response object types handled by
 * this request handler
 *
 * @return the comma separated list of response object types
 * @see DODSResponseObject
 * @see DODSResponseNames
 */
string
DODSRequestHandler::get_handler_names()
{
    string ret ;
    bool first_name = true ;
    DODSRequestHandler::Handler_citer i = _handler_list.begin() ;
    for( ; i != _handler_list.end(); i++ )
    {
	if( !first_name )
	    ret += ", " ;
	ret += (*i).first ;
	first_name = false ;
    }
    return ret ;
}

// $Log: DODSRequestHandler.cc,v $
// Revision 1.2  2004/09/09 17:17:12  pwest
// Added copywrite information
//
// Revision 1.1  2004/06/30 20:16:24  pwest
// dods dispatch code, can be used for apache modules or simple cgi script
// invocation or opendap daemon. Built during cedar server development.
//