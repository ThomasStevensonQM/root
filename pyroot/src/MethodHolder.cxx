// @(#)root/pyroot:$Name:  $:$Id: MethodHolder.cxx,v 1.24 2004/10/30 06:26:43 brun Exp $
// Author: Wim Lavrijsen, Apr 2004

// Bindings
#include "PyROOT.h"
#include "MethodHolder.h"
#include "ObjectHolder.h"
#include "PyBufferFactory.h"
#include "RootWrapper.h"

// ROOT
#include "TROOT.h"
#include "TClass.h"
#include "TObject.h"
#include "TString.h"
#include "TArray.h"
#include "TGlobal.h"
#include "TMethod.h"
#include "TMethodArg.h"
#include "TClassEdit.h"
#include "Gtypes.h"
#include "GuiTypes.h"
#include "Rtypes.h"

// CINT
#include "Api.h"
#include "TVirtualMutex.h"

// Standard
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <exception>
#include <map>
#include <string>
#include <iostream>


//- local helpers ---------------------------------------------------------------
namespace {

// converters for built-ins
   bool long_convert( PyObject* obj, G__CallFunc* func, void*& ) {
      func->SetArg( PyLong_AsLong( obj ) );
      if ( PyErr_Occurred() )
         return false;
      return true;
   }

   bool longlong_convert( PyObject* obj, G__CallFunc* func, void*& buf ) {
   // get value
      Long64_t ll = PyLong_AsLongLong( obj );
      if ( PyErr_Occurred() )
         return false;

   // create memory if necessary
      if ( ! buf )
         buf = reinterpret_cast< void* >( new Long64_t( 0 ) );

   // copy new value
      *(reinterpret_cast< Long64_t* >( buf )) = ll;

   // set value and declare success
      func->SetArg( (long) buf );
      return true;
   }

   bool double_convert( PyObject* obj, G__CallFunc* func, void*& ) {
      func->SetArg( PyFloat_AsDouble( obj ) );
      if ( PyErr_Occurred() )
         return false;
      return true;
   }

   bool cstring_convert( PyObject* obj, G__CallFunc* func, void*& buf ) {
   // construct a new string and copy it in new memory
      const char* s = PyString_AsString( obj );
      if ( PyErr_Occurred() )
         return false;

   // destroy old memory
      delete[] reinterpret_cast< char* >( buf );

   // copy the new string
      char* p = new char[ strlen( s ) + 1 ];
      strcpy( p, s );

   // store the new memory for deletion the next time around
      buf = reinterpret_cast< void* >( p );

   // set the value
      func->SetArg( reinterpret_cast< long >( p ) );

   // done, declare success
      return true;
   }

   bool tstring_convert( PyObject* obj, G__CallFunc* func, void*& buf ) {
   // all similar to cstring, see above
      const char* s = PyString_AsString( obj );
      if ( PyErr_Occurred() )
         return false;

      delete[] reinterpret_cast< TString* >( buf );
      TString* p = new TString( s );
      buf = reinterpret_cast< void* >( p );

      func->SetArg( reinterpret_cast< long >( p ) );

      return true;

   }

   bool void_convert( PyObject*, G__CallFunc* func, void*& ) {
   // TODO: verify to see if this is the proper approach
      func->SetArg( 0l );
      std::cerr << "convert< void > called ... may not be proper\n";
      return true;
   }


// traits for python's array type codes
#ifndef R__NO_CLASS_TEMPLATE_SPECIALIZATION
   template< class aType > struct tct {};
   template<> struct tct< int > { static const char tc; };
   template<> struct tct< long > { static const char tc; };
   template<> struct tct< float > { static const char tc; };
   template<> struct tct< double > { static const char tc; };
   const char tct< int >::tc = 'i';
   const char tct< long >::tc = 'l';
   const char tct< float >::tc = 'f';
   const char tct< double >::tc = 'd';
#else
   static char GetTct(int) { return 'i'; };
   static char GetTct(long) { return 'l'; };
   static char GetTct(float) { return 'f'; };
   static char GetTct(double) { return 'd'; };
#endif

// pointer/array conversions
   bool voidarray_convert( PyObject* obj, G__CallFunc* func, void*& ) {
   // just convert pointer if it is a ROOT object
      PyROOT::ObjectHolder* holder = PyROOT::Utility::getObjectHolder( obj );
      if ( holder != 0 ) {
      // this object can no longer be held, as the pointer to it may get copied
         holder->release();

      // set pointer
         func->SetArg( reinterpret_cast< long >( holder->getObject() ) );
         return true;
      }

   // special case: allow null pointer
      long val = PyLong_AsLong( obj );
      if ( PyErr_Occurred() )
         PyErr_Clear();
      else if ( val == 0 ) {
         func->SetArg( val );
         return true;
      }

   // special case: don't handle strings here (they're buffers, but not quite)
      if ( PyString_Check( obj ) )
         return false;
      
   // ok, then attempt to retrieve pointer to buffer interface
      PyBufferProcs* bufprocs = obj->ob_type->tp_as_buffer;
      PySequenceMethods* seqmeths = obj->ob_type->tp_as_sequence;
      if ( seqmeths != 0 && bufprocs != 0 && bufprocs->bf_getwritebuffer != 0 &&
           (*(bufprocs->bf_getsegcount))( obj, 0 ) == 1 ) {

      // get the buffer
         void* buf = 0;
         int buflen = (*(bufprocs->bf_getwritebuffer))( obj, 0, &buf );

      // determine buffer compatibility
         if ( buflen / (*(seqmeths->sq_length))( obj ) == sizeof( void* ) ) {
         // this is a gamble ... may or may not be ok, but that's for the user
            func->SetArg( (long) buf );
            return true;
         }
      }

   // give up
      return false;
   }

   template< class aType >
   bool carray_convert( PyObject* obj, G__CallFunc* func, void*& ) {
   // special case: don't handle strings here (they're buffers, but not quite)
      if ( PyString_Check( obj ) )
         return false;

   // attempt to retrieve pointer to buffer interface
      PyBufferProcs* bufprocs = obj->ob_type->tp_as_buffer;
      PySequenceMethods* seqmeths = obj->ob_type->tp_as_sequence;
      if ( seqmeths != 0 && bufprocs != 0 && bufprocs->bf_getwritebuffer != 0 &&
           (*(bufprocs->bf_getsegcount))( obj, 0 ) == 1 ) {

      // get the buffer
         void* buf = 0;
         int buflen = (*(bufprocs->bf_getwritebuffer))( obj, 0, &buf );

      // determine buffer compatibility (use "buf" as a status flag)
         PyObject* tc = PyObject_GetAttrString( obj, const_cast< char* >( "typecode" ) );
         if ( tc != 0 ) {                    // for array objects
   #ifndef R__NO_CLASS_TEMPLATE_SPECIALIZATION
            if ( PyString_AS_STRING( tc )[0] != tct< aType >::tc )
   #else
            if ( PyString_AS_STRING( tc )[0] != GetTct( (aType)0 ) )
   #endif
              
               buf = 0;                      // no match
            Py_DECREF( tc );
         }
         else if ( buflen / (*(seqmeths->sq_length))( obj ) == sizeof( aType ) ) {
         // this is a gamble ... may or may not be ok, but that's for the user
            PyErr_Clear();
         }
         else
            buf = 0;                         // not compatible

         if ( buf != 0 ) {
            func->SetArg( (long) buf );
            return true;
         }
      }

   // give up
      return false;
   }

   bool carray_convert_int( PyObject* obj, G__CallFunc* func, void*& ref) {
      return carray_convert< int >(obj,func,ref);
   }
   bool carray_convert_long( PyObject* obj, G__CallFunc* func, void*& ref) {
      return carray_convert< long >(obj,func,ref);
   }
   bool carray_convert_float( PyObject* obj, G__CallFunc* func, void*& ref) {
      return carray_convert< float >(obj,func,ref);
   }
   bool carray_convert_double( PyObject* obj, G__CallFunc* func, void*& ref) {
      return carray_convert< double >(obj,func,ref);
   }
 

// python C-API objects conversion
   static bool pyobject_convert( PyObject* obj, G__CallFunc* func, void*& ) {
      func->SetArg( reinterpret_cast< long >( obj ) );
      return true;
   }

// CINT temp level guard
   struct TempLevelGuard {
      TempLevelGuard() { G__settemplevel( 1 ); }
      ~TempLevelGuard() { G__settemplevel( -1 ); }
   };

} // unnamed namespace


//- data -----------------------------------------------------------------------
namespace {

   typedef std::pair< const char*, PyROOT::MethodHolder::cnvfct_t > ncp_t;

// handlers for ROOT types
   ncp_t handlers_[] = {
   // basic types
      ncp_t( "char",               &long_convert                      ),
      ncp_t( "unsigned char",      &long_convert                      ),
      ncp_t( "short",              &long_convert                      ),
      ncp_t( "unsigned short",     &long_convert                      ),
      ncp_t( "int",                &long_convert                      ),
      ncp_t( "unsigned int",       &long_convert                      ),
      ncp_t( "long",               &long_convert                      ),
      ncp_t( "unsigned long",      &long_convert                      ),
      ncp_t( "long long",          &longlong_convert                  ),
      ncp_t( "float",              &double_convert                    ),
      ncp_t( "double",             &double_convert                    ),
      ncp_t( "void",               &void_convert                      ),
      ncp_t( "bool",               &long_convert                      ),
      ncp_t( "const char*",        &cstring_convert                   ),
      ncp_t( "TString",            &tstring_convert                   ),

   // string type
      ncp_t( "char*",              &cstring_convert                   ),

   // pointer types
      ncp_t( "int*",               &carray_convert_int                ),
      ncp_t( "long*",              &carray_convert_long               ),
      ncp_t( "float*",             &carray_convert_float              ),
      ncp_t( "double*",            &carray_convert_double             ),

   // default pointer type
      ncp_t( "void*",              &voidarray_convert                 ),

   // python C-API objects
      ncp_t( "PyObject*",          &pyobject_convert                  ),
      ncp_t( "_object*",           &pyobject_convert                  )
   };

   const int nHandlers_ = sizeof( handlers_ ) / sizeof( handlers_[ 0 ] );

   typedef std::map< std::string, PyROOT::MethodHolder::cnvfct_t > Handlers_t;
   Handlers_t theHandlers;

   class InitHandlers_ {
   public:
      InitHandlers_() {
         for ( int i = 0; i < nHandlers_; ++i ) {
            theHandlers[ handlers_[ i ].first ] = handlers_[ i ].second;
         }
      }
   } initHandlers_;

} // unnamed namespace


//- private helpers ------------------------------------------------------------
inline void PyROOT::MethodHolder::copy_( const MethodHolder& om ) {
// yes, these pointer copy semantics are proper
   m_class       = om.m_class;
   m_method      = om.m_method;
   m_methodCall  = 0;
   m_offset      = 0;
   m_tagnum      = -1;

   m_argsConverters = om.m_argsConverters;
   m_callString     = om.m_callString;
   m_isInitialized  = om.m_isInitialized;
   m_lastObject     = 0;

// the new args buffer is clean
   m_argsBuffer.resize( om.m_argsBuffer.size() );

// only copy if available: this is a lazy cache value
   if ( om.m_methodCall ) {
      m_methodCall = new G__CallFunc( *om.m_methodCall );
   }
}


inline void PyROOT::MethodHolder::destroy_() const {
// no deletion of m_method (ROOT resposibility)
   delete m_methodCall;
// buffer is leaked for now
}


bool PyROOT::MethodHolder::initDispatch_() {
   assert( m_callString.length() == 0 );

// buffers for argument dispatching
   const int nArgs = m_method->GetNargs();
   if ( nArgs == 0 )
      return true;

   m_argsBuffer.resize( nArgs );        // zeroes as defaults
   m_argsConverters.resize( nArgs );    // id.

// setup the dispatch cache
   int iarg = 0;
   TIter nextarg( m_method->GetListOfMethodArgs() );
   while ( TMethodArg* arg = (TMethodArg*)nextarg() ) {
      G__TypeInfo argType = arg->GetTypeName();

      std::string fullType = arg->GetFullTypeName();
      std::string realType = argType.TrueName();

      if ( Utility::isPointer( fullType ) ) {
         Handlers_t::iterator h = theHandlers.find( realType + "*" );
         if ( h == theHandlers.end() ) {
            h = theHandlers.find( "void*" );
         }
         m_argsConverters[ iarg ] = h->second;
      }
      else if ( argType.Property() & G__BIT_ISENUM ) {
         m_argsConverters[ iarg ] = theHandlers.find( "UInt_t" )->second;
      }
      else {
         Handlers_t::iterator hit = theHandlers.find( realType );
         if ( hit != theHandlers.end() ) {
            m_argsConverters[ iarg ] = hit->second;
         }
         else {
            PyErr_SetString(
               PyExc_TypeError, ("argument type " + fullType + " not handled").c_str() );
            return false;
         }
      }

   // setup call string
      if ( m_callString.length() == 0 )
         m_callString = fullType;
      else
         m_callString += "," + fullType;

   // advance argument counter
      iarg += 1;
   }

   return true;
}


void PyROOT::MethodHolder::calcOffset_( void* obj, TClass* cls ) {
// loop optimization
   if ( obj == m_lastObject )
      return;
   m_lastObject = obj;

// actual offset calculation, as needed
   long derivedtagnum = cls->GetClassInfo()->Tagnum();

   if ( derivedtagnum != m_tagnum ) {
      m_offset = G__isanybase( m_class->GetClassInfo()->Tagnum(), derivedtagnum, (long) obj );
      m_tagnum = derivedtagnum;
   }
}


//- constructors and destructor ------------------------------------------------
PyROOT::MethodHolder::MethodHolder( TClass* cls, TMethod* tm ) :
      m_class( cls ), m_method( tm ), m_callString( "" ) {
   m_methodCall = 0;
   m_offset     = 0;
   m_tagnum     = -1;
   m_lastObject = 0;
   m_returnType = Utility::kOther;
   m_isInitialized = false;
}

PyROOT::MethodHolder::MethodHolder( const MethodHolder& om ) : PyCallable( om ) {
   copy_( om );
}


PyROOT::MethodHolder& PyROOT::MethodHolder::operator=( const MethodHolder& om ) {
   if ( this != &om ) {
      destroy_();
      copy_( om );
   }

   return *this;
}


PyROOT::MethodHolder::~MethodHolder() {
   destroy_();
}


//- protected members -----------------------------------------------------------
bool PyROOT::MethodHolder::execute( void* self ) {
   R__LOCKGUARD( gCINTMutex );
   TempLevelGuard g;

   try {
      m_methodCall->Exec( (void*) ( (long) self + m_offset ) );
   }
   catch ( std::exception& e ) {
      std::cout << "C++ exception caught: " << e.what() << std::endl;
      return false;
   }

   return true;
}


bool PyROOT::MethodHolder::execute( void* self, long& val ) {
   R__LOCKGUARD( gCINTMutex );
   TempLevelGuard g;

   try {
      val = m_methodCall->ExecInt( (void*) ( (long) self + m_offset ) );
   }
   catch ( std::exception& e ) {
      std::cout << "C++ exception caught: " << e.what() << std::endl;
      return false;
   }

   return true;
}


bool PyROOT::MethodHolder::execute( void* self, double& val ) {
   R__LOCKGUARD( gCINTMutex );
   TempLevelGuard g;

   try {
      val = m_methodCall->ExecDouble( (void*) ( (long) self + m_offset ) );
   }
   catch ( std::exception& e ) {
      std::cout << "C++ exception caught: " << e.what() << std::endl;
      return false;
   }

   return true;
}


//- public members -------------------------------------------------------------
bool PyROOT::MethodHolder::initialize() {
// done if cache is already setup
   if ( m_isInitialized == true )
      return true;

   if ( ! initDispatch_() )
      return false;

// determine effective return type
   std::string returnType = m_method->GetReturnTypeName();
   m_rtShortName = TClassEdit::ShortType( G__TypeInfo( returnType.c_str() ).TrueName(), 1 );
   m_returnType = Utility::effectiveType( returnType );

// setup call func
   assert( m_methodCall == 0 );

   m_methodCall = new G__CallFunc();
   m_methodCall->SetFuncProto(
      m_class->GetClassInfo(), m_method->GetName(), m_callString.c_str(), &m_offset );

// init done
   m_isInitialized = true;

   return true;
}


bool PyROOT::MethodHolder::setMethodArgs( PyObject* aTuple, int offset ) {
// clean slate
   m_methodCall->ResetArg();

   int argc = PyTuple_GET_SIZE( aTuple );

   int argGiven = argc - offset;
   int argRequired = m_method->GetNargs() - m_method->GetNargsOpt();
   int argMax = m_argsBuffer.size();

// argc must be between min and max number of arguments
   if ( argGiven < argRequired ) {
      char txt[ 256 ];
      sprintf( txt, "%s() takes at least %d arguments (%d given)",
         m_method->GetName(), argRequired, argGiven );
      PyErr_SetString( PyExc_TypeError, txt );
      return false;
   }
   else if ( argMax < argGiven ) {
      char txt[ 256 ];
      sprintf( txt, "%s() takes at most %d arguments (%d given)",
         m_method->GetName(), argMax, argGiven );
      PyErr_SetString( PyExc_TypeError, txt );
      return false;
   }

// convert the arguments to the method call array
   for ( int i = offset; i < argc; i++ ) {
      if ( ! m_argsConverters[ i - offset ](
              PyTuple_GET_ITEM( aTuple, i ), m_methodCall, m_argsBuffer[ i - offset ] ) ) {
         char txt[ 64 ];
         sprintf( txt, "could not convert argument %d", i );
         PyErr_SetString( PyExc_TypeError, txt );
         return false;
      }
   }

   return true;
}


PyObject* PyROOT::MethodHolder::callMethod( void* self ) {
// execute the method and translate return type
   switch ( m_returnType ) {
   case Utility::kFloat:
   case Utility::kDouble: {
      double returnValue;
      execute( self, returnValue );
      return PyFloat_FromDouble( returnValue );
   }
   case Utility::kString: {
      long returnValue = 0;
      execute( self, returnValue );
      return PyString_FromString( (char*) returnValue );
   }
   case Utility::kChar: {
      long buf = 0;
      execute( self, buf );
      char c[2]; c[1] = '\0';
      c[0] = (char) buf;
      return PyString_FromString( c );
   }
   case Utility::kBool:
   case Utility::kShort:
   case Utility::kInt: {
      long returnValue = 0;
      execute( self, returnValue );
      return PyInt_FromLong( returnValue );
   }
   case Utility::kLong: {
      long returnValue = 0;
      execute( self, returnValue );
      return PyLong_FromLong( returnValue );
   }
   case Utility::kUInt:
   case Utility::kULong: {
      unsigned long returnValue = 0;
      execute( self, (long&)returnValue );
      return PyLong_FromUnsignedLong( returnValue );
   }
   case Utility::kLongLong: {
      long returnValue = 0;
      execute( self, returnValue );
      return PyLong_FromLongLong( *(reinterpret_cast< Long64_t* >( returnValue )) );
   }
   case Utility::kVoid: {
      execute( self );
      Py_INCREF( Py_None );
      return Py_None;
   }
   case Utility::kSTLString: {
      long address = 0;
      execute( self, address );
      return PyString_FromString( ((std::string*)address)->c_str() );
   }
   case Utility::kOther: {
   // get a string representation of the return type
      TClass* cls = gROOT->GetClass( m_rtShortName.c_str() );
      if ( cls != 0 ) {
         long address = 0;
         execute( self, address );

      // return null object if failed
         if ( ! address ) {
            Py_INCREF( Py_None );
            return Py_None;
         }

      // upgrade to real class for TObject and TGlobal returns
         ObjectHolder* obh = 0;
         if ( m_rtShortName == "TGlobal" ) {
            TGlobal* g = (TGlobal*)address;
            cls = gROOT->GetClass( g->GetTypeName() );
            if ( ! cls )
               cls = TGlobal::Class();
            else {
               if ( Utility::isPointer( g->GetFullTypeName() ) ) {
                  obh = new AddressHolder( (void**)g->GetAddress(), cls, false );
                  return bindRootObject( obh, true );
               }
               else
                  obh = new ObjectHolder( (void*)g->GetAddress(), cls, false );
            }
         }
         else {
            TClass* clActual = cls->GetActualClass( (void*)address );
            if ( clActual ) {
               int offset = (cls != clActual) ? clActual->GetBaseClassOffset( cls ) : 0;
               address -= offset;
            }

            obh = new ObjectHolder( (void*)address, clActual, false );
         }

         return bindRootObject( obh );
      }

   // confused ...
      std::cout << "unsupported return type (" << m_rtShortName << "), returning void\n";
   }
   default:
      break;
   }

// pointer types
   if ( Utility::kDoublePtr <= m_returnType ) {
      long address;
      execute( self, address );

      if ( address ) {
         switch ( m_returnType ) {
         case Utility::kLongPtr: {
            return PyBufferFactory::getInstance()->PyBuffer_FromMemory( (long*)address );
         }
         case Utility::kIntPtr: {
            return PyBufferFactory::getInstance()->PyBuffer_FromMemory( (int*)address );
         }
         case Utility::kDoublePtr: {
            return PyBufferFactory::getInstance()->PyBuffer_FromMemory( (double*)address );
         }
         case Utility::kFloatPtr: {
            return PyBufferFactory::getInstance()->PyBuffer_FromMemory( (float*)address );
         }
         case Utility::kVoidPtr: {
            return PyLong_FromVoidPtr( (void*) address );
         }
         default:
            break;
         }
      }
      else {
         Py_INCREF( Py_None );
         return Py_None;
      }
   }

// still here? confused ...
   PyErr_SetString( PyExc_TypeError, "return type in method not handled" );
   return 0;
}


PyObject* PyROOT::MethodHolder::operator()( PyObject* aTuple, PyObject* /* aDict */ ) {
// precaution
   if ( aTuple == 0 )
      return 0;                              // should not happen

// setup as necessary
   if ( ! initialize() )
      return 0;                              // important: 0, not PyNone

// translate the arguments
   if ( ! setMethodArgs( aTuple, 1 ) )
      return 0;                              // important: 0, not PyNone

// start actual method invocation
   ObjectHolder* obh = Utility::getObjectHolder( PyTuple_GET_ITEM( aTuple, 0 ) );
   if ( ! ( obh && obh->getObject() ) ) {
      PyErr_SetString( PyExc_ReferenceError, "attempt to access a null-pointer" );
      return 0;
   }

   void* obj = obh->getObject();

// reset offset as appropriate
   calcOffset_( obj, obh->objectIsA() );

// execute function
   return callMethod( obj );
}
