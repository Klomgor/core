/* -*- Mode: C++; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */
/*
 * This file is part of the LibreOffice project.
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 *
 * This file incorporates work covered by the following license notice:
 *
 *   Licensed to the Apache Software Foundation (ASF) under one or more
 *   contributor license agreements. See the NOTICE file distributed
 *   with this work for additional information regarding copyright
 *   ownership. The ASF licenses this file to you under the Apache
 *   License, Version 2.0 (the "License"); you may not use this file
 *   except in compliance with the License. You may obtain a copy of
 *   the License at http://www.apache.org/licenses/LICENSE-2.0 .
 */

#include <com/sun/star/embed/ElementModes.hpp>
#include <com/sun/star/embed/InvalidStorageException.hpp>
#include <com/sun/star/embed/StorageWrappedTargetException.hpp>
#include <com/sun/star/embed/XTransactedObject.hpp>
#include <com/sun/star/packages/NoEncryptionException.hpp>
#include <com/sun/star/packages/WrongPasswordException.hpp>
#include <com/sun/star/ucb/NameClash.hpp>
#include <com/sun/star/ucb/SimpleFileAccess.hpp>

#include <com/sun/star/ucb/InteractiveIOException.hpp>
#include <com/sun/star/ucb/IOErrorCode.hpp>
#include <com/sun/star/container/ElementExistException.hpp>
#include <com/sun/star/lang/XComponent.hpp>
#include <com/sun/star/lang/WrappedTargetRuntimeException.hpp>
#include <com/sun/star/io/IOException.hpp>
#include <com/sun/star/io/XTruncate.hpp>
#include <com/sun/star/io/TempFile.hpp>
#include <com/sun/star/sdbc/XResultSet.hpp>
#include <com/sun/star/sdbc/XRow.hpp>

#include <comphelper/fileurl.hxx>
#include <comphelper/processfactory.hxx>
#include <comphelper/storagehelper.hxx>
#include <cppuhelper/queryinterface.hxx>
#include <cppuhelper/exc_hlp.hxx>

#include <osl/diagnose.h>
#include <tools/urlobj.hxx>
#include <unotools/ucbhelper.hxx>
#include <unotools/ucbstreamhelper.hxx>
#include <unotools/streamwrap.hxx>
#include <unotools/tempfile.hxx>
#include <ucbhelper/content.hxx>

#include "fsstorage.hxx"
#include "oinputstreamcontainer.hxx"
#include "ostreamcontainer.hxx"

using namespace ::com::sun::star;

FSStorage::FSStorage( const ::ucbhelper::Content& aContent,
                    sal_Int32 nMode,
                    uno::Reference< uno::XComponentContext > const & xContext )
: m_aURL( aContent.getURL() )
, m_aContent( aContent )
, m_nMode( nMode )
, m_xContext( xContext )
{
    OSL_ENSURE( !m_aURL.isEmpty(), "The URL must not be empty" );
    // TODO: use properties
    if ( !xContext.is() )
        throw uno::RuntimeException();

    GetContent();
}

FSStorage::~FSStorage()
{
    std::unique_lock aGuard( m_aMutex );
    osl_atomic_increment(&m_refCount); // to call dispose
    try {
        disposeImpl(aGuard);
    }
    catch( uno::RuntimeException& )
    {}
}

bool FSStorage::MakeFolderNoUI( std::u16string_view rFolder )
{
    INetURLObject aURL( rFolder );
    OUString aTitle = aURL.getName( INetURLObject::LAST_SEGMENT, true, INetURLObject::DecodeMechanism::WithCharset );
    aURL.removeSegment();
    ::ucbhelper::Content aParent;
    ::ucbhelper::Content aResultContent;

    if ( ::ucbhelper::Content::create( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                                 uno::Reference< ucb::XCommandEnvironment >(),
                                 comphelper::getProcessComponentContext(),
                                 aParent ) )
        return ::utl::UCBContentHelper::MakeFolder( aParent, aTitle, aResultContent );

    return false;
}

ucbhelper::Content& FSStorage::GetContent()
{
    std::unique_lock aGuard( m_aMutex );
    return m_aContent;
}

void FSStorage::CopyStreamToSubStream( const OUString& aSourceURL,
                                        const uno::Reference< embed::XStorage >& xDest,
                                        const OUString& aNewEntryName )
{
    if ( !xDest.is() )
        throw uno::RuntimeException();

    uno::Reference< ucb::XCommandEnvironment > xDummyEnv;
    ::ucbhelper::Content aSourceContent( aSourceURL, xDummyEnv, comphelper::getProcessComponentContext() );
    uno::Reference< io::XInputStream > xSourceInput = aSourceContent.openStream();

    if ( !xSourceInput.is() )
        throw io::IOException(); // TODO: error handling

    uno::Reference< io::XStream > xSubStream = xDest->openStreamElement(
                                                aNewEntryName,
                                                embed::ElementModes::READWRITE | embed::ElementModes::TRUNCATE );
    if ( !xSubStream.is() )
        throw uno::RuntimeException();

    uno::Reference< io::XOutputStream > xDestOutput = xSubStream->getOutputStream();
    if ( !xDestOutput.is() )
        throw uno::RuntimeException();

    ::comphelper::OStorageHelper::CopyInputToOutput( xSourceInput, xDestOutput );
    xDestOutput->closeOutput();
}

void FSStorage::CopyContentToStorage_Impl(ucbhelper::Content& rContent,
                                          const uno::Reference<embed::XStorage>& xDest)
{
    // get list of contents of the Content
    // create cursor for access to children
    uno::Sequence< OUString > aProps( 2 );
    OUString* pProps = aProps.getArray();
    pProps[0] = "TargetURL";
    pProps[1] = "IsFolder";

    try
    {
        uno::Reference<sdbc::XResultSet> xResultSet
            = rContent.createCursor(aProps, ::ucbhelper::INCLUDE_FOLDERS_AND_DOCUMENTS);
        uno::Reference< sdbc::XRow > xRow( xResultSet, uno::UNO_QUERY );
        if ( xResultSet.is() )
        {
            // go through the list: insert files as streams, insert folders as substorages using recursion
            while ( xResultSet->next() )
            {
                OUString aSourceURL( xRow->getString( 1 ) );
                bool bIsFolder( xRow->getBoolean(2) );

                // TODO/LATER: not sure whether the entry name must be encoded
                OUString aNewEntryName( INetURLObject( aSourceURL ).getName( INetURLObject::LAST_SEGMENT,
                                                                                    true,
                                                                                    INetURLObject::DecodeMechanism::NONE ) );
                if ( bIsFolder )
                {
                    uno::Reference< embed::XStorage > xSubStorage = xDest->openStorageElement( aNewEntryName,
                                                                                                embed::ElementModes::READWRITE );
                    if ( !xSubStorage.is() )
                        throw uno::RuntimeException();

                    uno::Reference< ucb::XCommandEnvironment > xDummyEnv;
                    ::ucbhelper::Content aSourceContent( aSourceURL, xDummyEnv, comphelper::getProcessComponentContext() );
                    CopyContentToStorage_Impl( aSourceContent, xSubStorage );
                }
                else
                {
                    CopyStreamToSubStream( aSourceURL, xDest, aNewEntryName );
                }
            }
        }

        uno::Reference< embed::XTransactedObject > xTransact( xDest, uno::UNO_QUERY );
        if ( xTransact.is() )
            xTransact->commit();
    }
    catch( ucb::InteractiveIOException& r )
    {
        if ( r.Code == ucb::IOErrorCode_NOT_EXISTING )
            OSL_FAIL( "The folder does not exist!" );
        else
            throw;
    }
}

//  XInterface

uno::Any SAL_CALL FSStorage::queryInterface( const uno::Type& rType )
{
    uno::Any aReturn = ::cppu::queryInterface
                (   rType
                ,   static_cast<lang::XTypeProvider*> ( this )
                ,   static_cast<embed::XStorage*> ( this )
                ,   static_cast<embed::XHierarchicalStorageAccess*> ( this )
                ,   static_cast<container::XNameAccess*> ( this )
                ,   static_cast<container::XElementAccess*> ( this )
                ,   static_cast<lang::XComponent*> ( this )
                ,   static_cast<beans::XPropertySet*> ( this ) );

    if ( aReturn.hasValue() )
        return aReturn ;

    return OWeakObject::queryInterface( rType );
}

void SAL_CALL FSStorage::acquire() noexcept
{
    OWeakObject::acquire();
}

void SAL_CALL FSStorage::release() noexcept
{
    OWeakObject::release();
}

//  XTypeProvider

uno::Sequence< uno::Type > SAL_CALL FSStorage::getTypes()
{
    static const uno::Sequence<uno::Type> aTypes {
        cppu::UnoType<lang::XTypeProvider>::get(),
        cppu::UnoType<embed::XStorage>::get(),
        cppu::UnoType<embed::XHierarchicalStorageAccess>::get(),
        cppu::UnoType<beans::XPropertySet>::get() };
    return aTypes;
}

uno::Sequence< sal_Int8 > SAL_CALL FSStorage::getImplementationId()
{
    return css::uno::Sequence<sal_Int8>();
}

//  XStorage

void SAL_CALL FSStorage::copyToStorage( const uno::Reference< embed::XStorage >& xDest )
{
    std::unique_lock aGuard( m_aMutex );

    if ( !xDest.is() || xDest == getXWeak() )
        throw lang::IllegalArgumentException(); // TODO:

    try
    {
        CopyContentToStorage_Impl( m_aContent, xDest );
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }
}

uno::Reference< io::XStream > SAL_CALL FSStorage::openStreamElement(
    const OUString& aStreamName, sal_Int32 nOpenMode )
{
    std::unique_lock aGuard( m_aMutex );
    return openStreamElementImpl(aGuard, aStreamName, nOpenMode);
}

uno::Reference< io::XStream > FSStorage::openStreamElementImpl(
    std::unique_lock<std::mutex>& /*rGuard*/,
    std::u16string_view aStreamName, sal_Int32 nOpenMode )
{
    // TODO/LATER: may need possibility to create folder if it was removed, since the folder can not be locked
    INetURLObject aFileURL( m_aURL );
    aFileURL.Append( aStreamName );

    if ( ::utl::UCBContentHelper::IsFolder( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw io::IOException();

    if ( ( nOpenMode & embed::ElementModes::NOCREATE )
      && !::utl::UCBContentHelper::IsDocument( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw io::IOException(); // TODO:

    uno::Reference< ucb::XCommandEnvironment > xDummyEnv; // TODO: provide InteractionHandler if any
    uno::Reference< io::XStream > xResult;
    try
    {
        if ( nOpenMode & embed::ElementModes::WRITE )
        {
            if ( aFileURL.GetProtocol() == INetProtocol::File )
            {
                uno::Reference<ucb::XSimpleFileAccess3> xSimpleFileAccess(
                    ucb::SimpleFileAccess::create( m_xContext ) );
                xResult = xSimpleFileAccess->openFileReadWrite( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
            }
            else
            {
                // TODO: test whether it really works for http and fwp
                std::unique_ptr<SvStream> pStream = ::utl::UcbStreamHelper::CreateStream( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                                                                          StreamMode::STD_WRITE );
                if ( pStream && !pStream->GetError() )
                    xResult.set( new ::utl::OStreamWrapper( std::move(pStream) ) );
            }

            if ( !xResult.is() )
                throw io::IOException();

            if ( nOpenMode & embed::ElementModes::TRUNCATE )
            {
                uno::Reference< io::XTruncate > xTrunc( xResult->getOutputStream(), uno::UNO_QUERY_THROW );
                xTrunc->truncate();
            }
        }
        else
        {
            if ( ( nOpenMode & embed::ElementModes::TRUNCATE )
              || !::utl::UCBContentHelper::IsDocument( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
                throw io::IOException(); // TODO: access denied

            ::ucbhelper::Content aResultContent( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDummyEnv, comphelper::getProcessComponentContext() );
            uno::Reference< io::XInputStream > xInStream = aResultContent.openStream();
            xResult = new OFSInputStreamContainer(xInStream);
        }
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( packages::WrongPasswordException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }

    return xResult;
}

uno::Reference< io::XStream > SAL_CALL FSStorage::openEncryptedStreamElement(
    const OUString&, sal_Int32, const OUString& )
{
    throw packages::NoEncryptionException();
}

uno::Reference< embed::XStorage > SAL_CALL FSStorage::openStorageElement(
            const OUString& aStorName, sal_Int32 nStorageMode )
{
    std::unique_lock aGuard( m_aMutex );
    return openStorageElementImpl(aGuard, aStorName, nStorageMode);
}

uno::Reference< embed::XStorage > FSStorage::openStorageElementImpl(
            std::unique_lock<std::mutex>& /*rGuard*/,
            std::u16string_view aStorName, sal_Int32 nStorageMode )
{
    if ( ( nStorageMode & embed::ElementModes::WRITE )
      && !( m_nMode & embed::ElementModes::WRITE ) )
          throw io::IOException(); // TODO: error handling

    // TODO/LATER: may need possibility to create folder if it was removed, since the folder can not be locked
    INetURLObject aFolderURL( m_aURL );
    aFolderURL.Append( aStorName );

    bool bFolderExists = ::utl::UCBContentHelper::IsFolder( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
    if ( !bFolderExists && ::utl::UCBContentHelper::IsDocument( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw io::IOException(); // TODO:

    if ( ( nStorageMode & embed::ElementModes::NOCREATE ) && !bFolderExists )
        throw io::IOException(); // TODO:

    uno::Reference< ucb::XCommandEnvironment > xDummyEnv; // TODO: provide InteractionHandler if any
    rtl::Reference< FSStorage > xResult;
    try
    {
        if ( nStorageMode & embed::ElementModes::WRITE )
        {
            if ( ( nStorageMode & embed::ElementModes::TRUNCATE ) && bFolderExists )
            {
                ::utl::UCBContentHelper::Kill( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
                bFolderExists =
                    MakeFolderNoUI( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ); // TODO: not atomic :(
            }
            else if ( !bFolderExists )
            {
                bFolderExists =
                    MakeFolderNoUI( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ); // TODO: not atomic :(
            }
        }
        else if ( nStorageMode & embed::ElementModes::TRUNCATE )
            throw io::IOException(); // TODO: access denied

        if ( !bFolderExists )
            throw io::IOException(); // there is no such folder

        ::ucbhelper::Content aResultContent( aFolderURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDummyEnv, comphelper::getProcessComponentContext() );
        xResult = new FSStorage( aResultContent, nStorageMode, m_xContext );
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }

    return xResult;
}

uno::Reference< io::XStream > SAL_CALL FSStorage::cloneStreamElement( const OUString& aStreamName )
{
    std::unique_lock aGuard( m_aMutex );

    // TODO/LATER: may need possibility to create folder if it was removed, since the folder can not be locked
    INetURLObject aFileURL( m_aURL );
    aFileURL.Append( aStreamName );

    rtl::Reference < utl::TempFileFastService > xTempResult;
    try
    {
        uno::Reference< ucb::XCommandEnvironment > xDummyEnv;
        ::ucbhelper::Content aResultContent( aFileURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDummyEnv, comphelper::getProcessComponentContext() );
        uno::Reference< io::XInputStream > xInStream = aResultContent.openStream();

        xTempResult = new utl::TempFileFastService;
        uno::Reference < io::XOutputStream > xTempOut = xTempResult->getOutputStream();
        uno::Reference < io::XInputStream > xTempIn = xTempResult->getInputStream();

        ::comphelper::OStorageHelper::CopyInputToOutput( xInStream, xTempOut );
        xTempOut->closeOutput();
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( packages::WrongPasswordException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }

    return xTempResult;
}

uno::Reference< io::XStream > SAL_CALL FSStorage::cloneEncryptedStreamElement(
    const OUString&,
    const OUString& )
{
    throw packages::NoEncryptionException();
}

void SAL_CALL FSStorage::copyLastCommitTo(
            const uno::Reference< embed::XStorage >& xTargetStorage )
{
    copyToStorage( xTargetStorage );
}

void SAL_CALL FSStorage::copyStorageElementLastCommitTo(
            const OUString& aStorName,
            const uno::Reference< embed::XStorage >& xTargetStorage )
{
    std::unique_lock aGuard( m_aMutex );

    uno::Reference< embed::XStorage > xSourceStor( openStorageElementImpl(aGuard, aStorName, embed::ElementModes::READ),
                                                    uno::UNO_SET_THROW );
    xSourceStor->copyToStorage( xTargetStorage );
}

sal_Bool SAL_CALL FSStorage::isStreamElement( const OUString& aElementName )
{
    std::unique_lock aGuard( m_aMutex );

    INetURLObject aURL( m_aURL );
    aURL.Append( aElementName );

    return !::utl::UCBContentHelper::IsFolder( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
}

sal_Bool SAL_CALL FSStorage::isStorageElement( const OUString& aElementName )
{
    std::unique_lock aGuard( m_aMutex );

    INetURLObject aURL( m_aURL );
    aURL.Append( aElementName );

    return ::utl::UCBContentHelper::IsFolder( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
}

void SAL_CALL FSStorage::removeElement( const OUString& aElementName )
{
    std::unique_lock aGuard( m_aMutex );

    INetURLObject aURL( m_aURL );
    aURL.Append( aElementName );

    if ( !::utl::UCBContentHelper::IsFolder( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) )
      && !::utl::UCBContentHelper::IsDocument( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw container::NoSuchElementException(); // TODO:

    ::utl::UCBContentHelper::Kill( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) );
}

void SAL_CALL FSStorage::renameElement( const OUString& aElementName, const OUString& aNewName )
{
    std::unique_lock aGuard( m_aMutex );

    INetURLObject aOldURL( m_aURL );
    aOldURL.Append( aElementName );

    INetURLObject aNewURL( m_aURL );
    aNewURL.Append( aNewName );

    if ( !::utl::UCBContentHelper::IsFolder( aOldURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) )
      && !::utl::UCBContentHelper::IsDocument( aOldURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw container::NoSuchElementException(); // TODO:

    if ( ::utl::UCBContentHelper::IsFolder( aNewURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) )
      || ::utl::UCBContentHelper::IsDocument( aNewURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
          throw container::ElementExistException(); // TODO:

    try
    {
        uno::Reference< ucb::XCommandEnvironment > xDummyEnv;
        ::ucbhelper::Content aSourceContent( aOldURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDummyEnv, comphelper::getProcessComponentContext() );

        m_aContent.transferContent(aSourceContent, ::ucbhelper::InsertOperation::Move, aNewName,
                                     ucb::NameClash::ERROR);
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( container::NoSuchElementException& )
    {
        throw;
    }
    catch( container::ElementExistException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }
}

void FSStorage::copyElementToImpl(std::unique_lock<std::mutex>& /*rGuard*/,
                                  std::u16string_view aElementName,
                                  const uno::Reference< embed::XStorage >& xDest,
                                  const OUString& aNewName )
{
    if ( !xDest.is() )
        throw uno::RuntimeException();

    INetURLObject aOwnURL( m_aURL );
    aOwnURL.Append( aElementName );

    if ( xDest->hasByName( aNewName ) )
          throw container::ElementExistException(); // TODO:

    try
    {
        uno::Reference< ucb::XCommandEnvironment > xDummyEnv;
        if ( ::utl::UCBContentHelper::IsFolder( aOwnURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        {
            ::ucbhelper::Content aSourceContent( aOwnURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDummyEnv, comphelper::getProcessComponentContext() );
            uno::Reference< embed::XStorage > xDestSubStor(
                                    xDest->openStorageElement( aNewName, embed::ElementModes::READWRITE ),
                                    uno::UNO_SET_THROW );

            CopyContentToStorage_Impl( aSourceContent, xDestSubStor );
        }
        else if ( ::utl::UCBContentHelper::IsDocument( aOwnURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        {
            CopyStreamToSubStream( aOwnURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ), xDest, aNewName );
        }
        else
            throw container::NoSuchElementException(); // TODO:
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( container::NoSuchElementException& )
    {
        throw;
    }
    catch( container::ElementExistException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }
}

void SAL_CALL FSStorage::copyElementTo( const OUString& aElementName,
                                        const uno::Reference< embed::XStorage >& xDest,
                                        const OUString& aNewName )
{
    std::unique_lock aGuard( m_aMutex );
    copyElementToImpl(aGuard, aElementName, xDest, aNewName);
}

void SAL_CALL FSStorage::moveElementTo( const OUString& aElementName,
                                        const uno::Reference< embed::XStorage >& xDest,
                                        const OUString& aNewName )
{
    std::unique_lock aGuard( m_aMutex );
    copyElementToImpl(aGuard, aElementName, xDest, aNewName);

    INetURLObject aOwnURL( m_aURL );
    aOwnURL.Append( aElementName );
    if ( !::utl::UCBContentHelper::Kill( aOwnURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        throw io::IOException(); // TODO: error handling
}

//  XNameAccess

uno::Any SAL_CALL FSStorage::getByName( const OUString& aName )
{
    std::unique_lock aGuard( m_aMutex );

    if ( aName.isEmpty() )
        throw lang::IllegalArgumentException();

    uno::Any aResult;
    try
    {
        INetURLObject aURL( m_aURL );
        aURL.Append( aName );


        if ( ::utl::UCBContentHelper::IsFolder( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        {
            aResult <<= openStorageElementImpl( aGuard, aName, embed::ElementModes::READ );
        }
        else if ( ::utl::UCBContentHelper::IsDocument( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) )
        {
            aResult <<= openStreamElementImpl( aGuard, aName, embed::ElementModes::READ );
        }
        else
            throw container::NoSuchElementException(); // TODO:
    }
    catch (const container::NoSuchElementException&)
    {
        throw;
    }
    catch (const lang::WrappedTargetException&)
    {
        throw;
    }
    catch (const uno::RuntimeException&)
    {
        throw;
    }
    catch (const uno::Exception&)
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw lang::WrappedTargetException( u"Can not open element!"_ustr,
                                            getXWeak(),
                                            aCaught );
    }

    return aResult;
}


uno::Sequence< OUString > SAL_CALL FSStorage::getElementNames()
{
    std::unique_lock aGuard( m_aMutex );

    uno::Sequence< OUString > aResult;

    try
    {
        uno::Sequence<OUString> aProps { u"Title"_ustr };

        sal_Int32 nSize = 0;
        uno::Reference<sdbc::XResultSet> xResultSet
            = m_aContent.createCursor(aProps, ::ucbhelper::INCLUDE_FOLDERS_AND_DOCUMENTS);
        uno::Reference< sdbc::XRow > xRow( xResultSet, uno::UNO_QUERY );
        if ( xResultSet.is() )
        {
            // go through the list
            while ( xResultSet->next() )
            {
                aResult.realloc( ++nSize );
                aResult.getArray()[nSize-1] = xRow->getString(1);
            }
        }
    }
    catch( const ucb::InteractiveIOException& r )
    {
        if ( r.Code == ucb::IOErrorCode_NOT_EXISTING )
            OSL_FAIL( "The folder does not exist!" );
        else
        {
            uno::Any aCaught( ::cppu::getCaughtException() );
            throw lang::WrappedTargetRuntimeException( u"Can not open storage!"_ustr,
                                            getXWeak(),
                                            aCaught );
        }
    }
    catch (const uno::RuntimeException&)
    {
        throw;
    }
    catch (const uno::Exception&)
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw lang::WrappedTargetRuntimeException( u"Can not open storage!"_ustr,
                                            getXWeak(),
                                            aCaught );
    }

    return aResult;
}

sal_Bool SAL_CALL FSStorage::hasByName( const OUString& aName )
{
    std::unique_lock aGuard( m_aMutex );

    if ( aName.isEmpty() )
        throw lang::IllegalArgumentException();

    INetURLObject aURL( m_aURL );
    aURL.Append( aName );

    return ( ::utl::UCBContentHelper::IsFolder( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) )
      || ::utl::UCBContentHelper::IsDocument( aURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ) ) );
}

uno::Type SAL_CALL FSStorage::getElementType()
{
    // it is a multitype container
    return uno::Type();
}

sal_Bool SAL_CALL FSStorage::hasElements()
{
    std::unique_lock aGuard( m_aMutex );

    try
    {
        uno::Sequence<OUString> aProps { u"TargetURL"_ustr };

        uno::Reference<sdbc::XResultSet> xResultSet
            = m_aContent.createCursor(aProps, ::ucbhelper::INCLUDE_FOLDERS_AND_DOCUMENTS);
        return ( xResultSet.is() && xResultSet->next() );
    }
    catch (const uno::RuntimeException&)
    {
        throw;
    }
    catch (const uno::Exception& ex)
    {
        css::uno::Any anyEx = cppu::getCaughtException();
        throw lang::WrappedTargetRuntimeException( ex.Message,
                        nullptr, anyEx );
    }
}

//  XDisposable
void SAL_CALL FSStorage::dispose()
{
    std::unique_lock aGuard( m_aMutex );
    disposeImpl(aGuard);
}

void FSStorage::disposeImpl(std::unique_lock<std::mutex>& rGuard)
{
    if ( m_aListenersContainer.getLength(rGuard) )
    {
        lang::EventObject aSource( getXWeak() );
        m_aListenersContainer.disposeAndClear( rGuard, aSource );
    }
}

void SAL_CALL FSStorage::addEventListener(
            const uno::Reference< lang::XEventListener >& xListener )
{
    std::unique_lock aGuard( m_aMutex );

    m_aListenersContainer.addInterface( aGuard, xListener );
}

void SAL_CALL FSStorage::removeEventListener(
            const uno::Reference< lang::XEventListener >& xListener )
{
    std::unique_lock aGuard( m_aMutex );

    m_aListenersContainer.removeInterface( aGuard, xListener );
}

//  XPropertySet

uno::Reference< beans::XPropertySetInfo > SAL_CALL FSStorage::getPropertySetInfo()
{
    //TODO:
    return uno::Reference< beans::XPropertySetInfo >();
}


void SAL_CALL FSStorage::setPropertyValue( const OUString& aPropertyName, const uno::Any& )
{
    if ( aPropertyName == "URL" || aPropertyName == "OpenMode" )
        throw beans::PropertyVetoException(); // TODO
    else
        throw beans::UnknownPropertyException(aPropertyName); // TODO
}


uno::Any SAL_CALL FSStorage::getPropertyValue( const OUString& aPropertyName )
{
    std::unique_lock aGuard( m_aMutex );

    if ( aPropertyName == "URL" )
        return uno::Any( m_aURL );
    else if ( aPropertyName == "OpenMode" )
        return uno::Any( m_nMode );

    throw beans::UnknownPropertyException(aPropertyName); // TODO
}


void SAL_CALL FSStorage::addPropertyChangeListener(
            const OUString& /*aPropertyName*/,
            const uno::Reference< beans::XPropertyChangeListener >& /*xListener*/ )
{
    //TODO:
}


void SAL_CALL FSStorage::removePropertyChangeListener(
            const OUString& /*aPropertyName*/,
            const uno::Reference< beans::XPropertyChangeListener >& /*aListener*/ )
{
    //TODO:
}


void SAL_CALL FSStorage::addVetoableChangeListener(
            const OUString& /*PropertyName*/,
            const uno::Reference< beans::XVetoableChangeListener >& /*aListener*/ )
{
    //TODO:
}


void SAL_CALL FSStorage::removeVetoableChangeListener(
            const OUString& /*PropertyName*/,
            const uno::Reference< beans::XVetoableChangeListener >& /*aListener*/ )
{
    //TODO:
}

//  XHierarchicalStorageAccess
uno::Reference< embed::XExtendedStorageStream > SAL_CALL FSStorage::openStreamElementByHierarchicalName( const OUString& sStreamPath, ::sal_Int32 nOpenMode )
{
    std::unique_lock aGuard( m_aMutex );

    if ( sStreamPath.toChar() == '/' )
        throw lang::IllegalArgumentException();

    INetURLObject aBaseURL( m_aURL );
    if ( !aBaseURL.setFinalSlash() )
        throw uno::RuntimeException();

    OUString aFileURL = INetURLObject::GetAbsURL(
                aBaseURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                sStreamPath );

    if ( ::utl::UCBContentHelper::IsFolder( aFileURL ) )
        throw io::IOException();

    if ( ( nOpenMode & embed::ElementModes::NOCREATE )
      && !::utl::UCBContentHelper::IsDocument( aFileURL ) )
        throw io::IOException(); // TODO:

    uno::Reference< ucb::XCommandEnvironment > xDummyEnv; // TODO: provide InteractionHandler if any
    uno::Reference< io::XStream > xResult;
    try
    {
        if ( nOpenMode & embed::ElementModes::WRITE )
        {
            if ( comphelper::isFileUrl( aFileURL ) )
            {
                uno::Reference<ucb::XSimpleFileAccess3> xSimpleFileAccess(
                    ucb::SimpleFileAccess::create( m_xContext ) );
                uno::Reference< io::XStream > xStream =
                    xSimpleFileAccess->openFileReadWrite( aFileURL );

                xResult = new OFSStreamContainer(xStream);
            }
            else
            {
                // TODO: test whether it really works for http and fwp
                std::unique_ptr<SvStream> pStream = ::utl::UcbStreamHelper::CreateStream( aFileURL,
                                                                          StreamMode::STD_WRITE );
                if ( pStream && !pStream->GetError() )
                {
                    uno::Reference< io::XStream > xStream( new ::utl::OStreamWrapper( std::move(pStream) ) );
                    xResult = new OFSStreamContainer(xStream);
                }
            }

            if ( !xResult.is() )
                throw io::IOException();

            if ( nOpenMode & embed::ElementModes::TRUNCATE )
            {
                uno::Reference< io::XTruncate > xTrunc( xResult->getOutputStream(), uno::UNO_QUERY_THROW );
                xTrunc->truncate();
            }
        }
        else
        {
            if ( ( nOpenMode & embed::ElementModes::TRUNCATE )
              || !::utl::UCBContentHelper::IsDocument( aFileURL ) )
                throw io::IOException(); // TODO: access denied

            ::ucbhelper::Content aResultContent( aFileURL, xDummyEnv, comphelper::getProcessComponentContext() );
            uno::Reference< io::XInputStream > xInStream = aResultContent.openStream();
            xResult = new OFSInputStreamContainer(xInStream);
        }
    }
    catch( embed::InvalidStorageException& )
    {
        throw;
    }
    catch( lang::IllegalArgumentException& )
    {
        throw;
    }
    catch( packages::WrongPasswordException& )
    {
        throw;
    }
    catch( embed::StorageWrappedTargetException& )
    {
        throw;
    }
    catch( io::IOException& )
    {
        throw;
    }
    catch( uno::RuntimeException& )
    {
        throw;
    }
    catch( uno::Exception& )
    {
        uno::Any aCaught( ::cppu::getCaughtException() );
        throw embed::StorageWrappedTargetException(u"Can't copy raw stream"_ustr,
                                                 uno::Reference< io::XInputStream >(),
                                                 aCaught );
    }

    return uno::Reference< embed::XExtendedStorageStream >( xResult, uno::UNO_QUERY_THROW );
}

uno::Reference< embed::XExtendedStorageStream > SAL_CALL FSStorage::openEncryptedStreamElementByHierarchicalName( const OUString& /*sStreamName*/, ::sal_Int32 /*nOpenMode*/, const OUString& /*sPassword*/ )
{
    throw packages::NoEncryptionException();
}

void SAL_CALL FSStorage::removeStreamElementByHierarchicalName( const OUString& sStreamPath )
{
    std::unique_lock aGuard( m_aMutex );

    // TODO/LATER: may need possibility to create folder if it was removed, since the folder can not be locked
    INetURLObject aBaseURL( m_aURL );
    if ( !aBaseURL.setFinalSlash() )
        throw uno::RuntimeException();

    OUString aFileURL = INetURLObject::GetAbsURL(
                aBaseURL.GetMainURL( INetURLObject::DecodeMechanism::NONE ),
                sStreamPath );

    if ( !::utl::UCBContentHelper::IsDocument( aFileURL ) )
    {
        if ( ::utl::UCBContentHelper::IsFolder( aFileURL ) )
            throw lang::IllegalArgumentException();
        else
            throw container::NoSuchElementException(); // TODO:
    }

    if ( !::utl::UCBContentHelper::Kill( aFileURL ) )
        throw io::IOException(); // TODO: error handling
}


/* vim:set shiftwidth=4 softtabstop=4 expandtab: */
