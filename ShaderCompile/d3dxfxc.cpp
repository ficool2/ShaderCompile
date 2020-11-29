//====== Copyright © 1996-2006, Valve Corporation, All rights reserved. =======//
//
// Purpose: D3DX command implementation.
//
// $NoKeywords: $
//
//=============================================================================//

#define WIN32_LEAN_AND_MEAN
#define NOWINRES
#define NOSERVICE
#define NOMCX
#define NOIME
#define NOMINMAX

#include "d3dxfxc.h"

#include "basetypes.h"
#include "cfgprocessor.h"
#include "cmdsink.h"
#ifdef USE_DXC
#include "dxc/Support/Global.h"
#include "dxc/Support/Unicode.h"
#include "dxc/Support/WinIncludes.h"
#include "dxc/Support/WinFunctions.h"
#include "dxc/dxcapi.h"
#else
#include "d3dcompiler.h"
#endif
#include "gsl/narrow"
#include <malloc.h>
#include <vector>

#ifndef USE_DXC
#pragma comment( lib, "D3DCompiler" )
#endif

CSharedFile::CSharedFile( std::vector<char>&& data ) noexcept : std::vector<char>( std::forward<std::vector<char>>( data ) )
{
}

void FileCache::Add( const std::string& fileName, std::vector<char>&& data )
{
	const auto& it = m_map.find( fileName );
	if ( it != m_map.end() )
		return;

	CSharedFile file( std::forward<std::vector<char>>( data ) );
	m_map.emplace( fileName, std::move( file ) );
}

const CSharedFile* FileCache::Get( const std::string& filename ) const
{
	// Search the cache first
	const auto find = m_map.find( filename );
	if ( find != m_map.cend() )
		return &find->second;
	return nullptr;
}

void FileCache::Clear()
{
	m_map.clear();
}

FileCache fileCache;

#ifndef USE_DXC
static struct DxIncludeImpl final : public ID3DInclude
{
	STDMETHOD( Open )( THIS_ D3D_INCLUDE_TYPE, LPCSTR pFileName, LPCVOID, LPCVOID* ppData, UINT* pBytes ) override
	{
		const CSharedFile* file = fileCache.Get( pFileName );
		if ( !file )
			return E_FAIL;

		*ppData = file->Data();
		*pBytes = gsl::narrow<UINT>( file->Size() );

		return S_OK;
	}

	STDMETHOD( Close )( THIS_ LPCVOID ) override
	{
		return S_OK;
	}

	virtual ~DxIncludeImpl() = default;
} s_incDxImpl;

class CResponse final : public CmdSink::IResponse
{
public:
	explicit CResponse( ID3DBlob* pShader, ID3DBlob* pListing, HRESULT hr ) noexcept
		: m_pShader( pShader )
		, m_pListing( pListing )
		, m_hr( hr )
	{
	}

	~CResponse() override
	{
		if ( m_pShader )
			m_pShader->Release();

		if ( m_pListing )
			m_pListing->Release();
	}

	bool Succeeded() const noexcept override { return m_pShader && m_hr == S_OK; }
	size_t GetResultBufferLen() const override { return Succeeded() ? m_pShader->GetBufferSize() : 0; }
	const void* GetResultBuffer() const override { return Succeeded() ? m_pShader->GetBufferPointer() : nullptr; }
	const char* GetListing() const override { return static_cast<const char*>( m_pListing ? m_pListing->GetBufferPointer() : nullptr ); }

protected:
	ID3DBlob* const m_pShader;
	ID3DBlob* const m_pListing;
	const HRESULT m_hr;
};
#else
struct DxcIncludeHandler : public IDxcIncludeHandler
{
private:
	volatile std::atomic<long> m_dwRef = { 0 };
	CComPtr<IDxcUtils> m_pUtils;
public:
	DxcIncludeHandler( CComPtr<IDxcUtils>&& utils ) : m_pUtils( std::forward<CComPtr<IDxcUtils>>( utils ) ) {}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return static_cast<ULONG>( ++m_dwRef );
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		ULONG result = static_cast<ULONG>( --m_dwRef );
		if ( result == 0 )
			delete this;
		return result;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface( REFIID iid, void** ppvObject )
	{
		if ( ppvObject == nullptr )
			return E_POINTER;

		// Support INoMarshal to void GIT shenanigans.
		if ( IsEqualIID( iid, __uuidof( IUnknown ) ) || IsEqualIID( iid, __uuidof( INoMarshal ) ) )
		{
			*ppvObject = static_cast<IUnknown*>( this );
			AddRef();
			return S_OK;
		}

		if ( IsEqualIID( iid, __uuidof( IDxcIncludeHandler ) ) )
		{
			*reinterpret_cast<IDxcIncludeHandler**>( ppvObject ) = this;
			AddRef();
			return S_OK;
		}

		return E_NOINTERFACE;
	}

	HRESULT STDMETHODCALLTYPE LoadSource( LPCWSTR pFilename, IDxcBlob** ppIncludeSource ) override
	{
		const int offset = pFilename[0] == L'.' && pFilename[1] == L'/' ? 2 : 0;
		std::string name( wcslen( pFilename ) - offset, ' ' );
		std::transform( pFilename + offset, pFilename + offset + name.size(), name.begin(), []( const wchar_t f ) { return static_cast<char>( f ); } );
		const CSharedFile* file = fileCache.Get( name );
		if ( !file )
			return E_FAIL;

		CComPtr<IDxcBlobEncoding> pMainFile;
		m_pUtils->CreateBlobFromPinned( file->Data(), gsl::narrow<UINT>( file->Size() ), DXC_CP_UTF8, &pMainFile );
		*ppIncludeSource = pMainFile.Detach();
		return S_OK;
	}
};

class DxcOutput final : public CmdSink::IResponse
{
	CComPtr<IDxcResult> m_pResult;
	CComPtr<IDxcBlob> m_pByteCode;
	CComPtr<IDxcBlobUtf8> m_pErrorBuf;
public:
	DxcOutput( CComPtr<IDxcResult>&& result ) : m_pResult( std::forward<CComPtr<IDxcResult>>( result ) )
	{
		m_pResult->GetOutput( DXC_OUT_OBJECT, IID_PPV_ARGS( &m_pByteCode ), nullptr );
		m_pResult->GetOutput( DXC_OUT_ERRORS, IID_PPV_ARGS( &m_pErrorBuf ), nullptr );
	}
	DxcOutput() = default;
	~DxcOutput() = default;

	bool Succeeded() const override { return m_pResult && m_pResult->HasOutput( DXC_OUT_OBJECT ); }
	size_t GetResultBufferLen() const override { return m_pByteCode ? m_pByteCode->GetBufferSize() : 0; }
	const void* GetResultBuffer() const override { return m_pByteCode ? m_pByteCode->GetBufferPointer() : nullptr; }
	const char* GetListing() const override { return m_pErrorBuf ? m_pErrorBuf->GetStringPointer() : nullptr; }
};
#endif

#ifdef WIN32
#define DXC_FAILED FAILED
#endif

void Compiler::ExecuteCommand( const CfgProcessor::ComboBuildCommand& pCommand, CmdSink::IResponse* &pResponse, unsigned int flags )
{
#ifdef USE_DXC
	CComPtr<IDxcUtils> pUtils;
	CComPtr<IDxcCompiler3> pCompiler;
	DxcCreateInstance( CLSID_DxcUtils, IID_PPV_ARGS( &pUtils ) );
	DxcCreateInstance( CLSID_DxcCompiler, IID_PPV_ARGS( &pCompiler ) );

	std::vector<DxcDefine> macros;
	macros.resize( pCommand.defines.size() );
	std::transform( pCommand.defines.cbegin(), pCommand.defines.cend(), macros.begin(), []( const auto& d ) { return DxcDefine{ d.first.data(), d.second.data() }; } );

	LPCWSTR args[5];
	uint32_t argc = 0;
	if ( flags & D3DCOMPILE_SKIP_VALIDATION )
		args[argc++] = DXC_ARG_SKIP_VALIDATION;
	if ( flags & D3DCOMPILE_AVOID_FLOW_CONTROL )
		args[argc++] = DXC_ARG_AVOID_FLOW_CONTROL;
	else if ( flags & D3DCOMPILE_PREFER_FLOW_CONTROL )
		args[argc++] = DXC_ARG_PREFER_FLOW_CONTROL;
	if ( flags & D3DCOMPILE_DEBUG )
		args[argc++] = DXC_ARG_DEBUG;
	if ( flags & D3DCOMPILE_DEBUG_NAME_FOR_SOURCE )
		args[argc++] = DXC_ARG_DEBUG_NAME_FOR_SOURCE;
	if ( flags & D3DCOMPILE_OPTIMIZATION_LEVEL0 )
		args[argc++] = DXC_ARG_OPTIMIZATION_LEVEL0;
	else if ( flags & D3DCOMPILE_OPTIMIZATION_LEVEL1 )
		args[argc++] = DXC_ARG_OPTIMIZATION_LEVEL1;
	else if ( flags & D3DCOMPILE_OPTIMIZATION_LEVEL2 )
		args[argc++] = DXC_ARG_OPTIMIZATION_LEVEL2;
	else if ( flags & D3DCOMPILE_OPTIMIZATION_LEVEL3 )
		args[argc++] = DXC_ARG_OPTIMIZATION_LEVEL3;

	CComPtr<IDxcCompilerArgs> pArgs;
	if ( DXC_FAILED( pUtils->BuildArguments( pCommand.fileName.data(), pCommand.entryPoint.data(), pCommand.shaderModel.data(), args, argc, macros.data(), macros.size(), &pArgs ) ) )
	{
		pResponse = new( std::nothrow ) DxcOutput();
		return;
	}

	CComPtr<IDxcBlob> pMainFile;
	CComPtr<IDxcIncludeHandler> pIncludeHandler = new( std::nothrow ) DxcIncludeHandler( std::move( pUtils ) );
	if ( DXC_FAILED( pIncludeHandler->LoadSource( pCommand.fileName.data(), &pMainFile ) ) )
	{
		pResponse = new( std::nothrow ) DxcOutput();
		return;
	}

	const DxcBuffer source{ pMainFile->GetBufferPointer(), pMainFile->GetBufferSize(), DXC_CP_UTF8 };

	CComPtr<IDxcResult> pResult;
	pCompiler->Compile( &source, pArgs->GetArguments(), pArgs->GetCount(), pIncludeHandler, IID_PPV_ARGS( &pResult ) );
	pResponse = new( std::nothrow ) DxcOutput( std::move( pResult ) );
#else
	// Macros to be defined for D3DX
	std::vector<D3D_SHADER_MACRO> macros;
	macros.resize( pCommand.defines.size() + 1 );
	std::transform( pCommand.defines.cbegin(), pCommand.defines.cend(), macros.begin(), []( const auto& d ) { return D3D_SHADER_MACRO{ d.first.data(), d.second.data() }; } );

	ID3DBlob* pShader        = nullptr; // NOTE: Must release the COM interface later
	ID3DBlob* pErrorMessages = nullptr; // NOTE: Must release COM interface later

	LPCVOID lpcvData = nullptr;
	UINT numBytes    = 0;
	HRESULT hr       = s_incDxImpl.Open( D3D_INCLUDE_LOCAL, pCommand.fileName.data(), nullptr, &lpcvData, &numBytes );
	if ( !FAILED( hr ) )
	{
		hr = D3DCompile( lpcvData, numBytes, pCommand.fileName.data(), macros.data(), &s_incDxImpl, pCommand.entryPoint.data(), pCommand.shaderModel.data(), flags, 0, &pShader, &pErrorMessages );

		// Close the file
		s_incDxImpl.Close( lpcvData );
	}

	pResponse = new( std::nothrow ) CResponse( pShader, pErrorMessages, hr );
#endif
}