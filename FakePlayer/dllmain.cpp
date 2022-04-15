// dllmain.cpp : Defines the entry point for the DLL application.
#include <iostream>

#include "framework.h"

uintptr_t find_pattern( std::string_view module_name, std::string_view pattern )
{
    const auto module_handle = GetModuleHandleA( module_name.data() );
    if ( !module_handle )
        return {};

    MODULEINFO module_info;
    GetModuleInformation( GetCurrentProcess(), module_handle, &module_info, sizeof( MODULEINFO ) );

    const auto image_size = module_info.SizeOfImage;

    if ( !image_size )
        return {};

    const auto base = reinterpret_cast<unsigned char*>( module_handle );
    static auto pattern_to_byte = []( const char* pattern )
    {
        auto bytes = std::vector<int>{};
        const auto start = const_cast<char*>( pattern );
        const auto end = const_cast<char*>( pattern ) + strlen( pattern );

        for ( auto current = start; current < end; ++current )
        {
            if ( *current == '?' )
            {
                ++current;

                if ( *current == '?' )
                    ++current;

                bytes.push_back( -1 );
            } else
            {
                bytes.push_back( strtoul( current, &current, 16 ) );
            }
        }
        return bytes;
    };

    const auto bytes = pattern_to_byte( pattern.data() );
    for ( auto i = 0UL; i < image_size - bytes.size(); ++i )
    {
        bool bByteFound = true;

        for ( unsigned long s = 0UL; s < bytes.size(); ++s )
        {
            if ( base[ i + s ] != bytes[ s ] && bytes[ s ] != -1 )
            {
                bByteFound = false;
                break;
            }
        }

        if ( bByteFound )
            return reinterpret_cast<std::uintptr_t>( &base[ i ] );
    }

    return {};
}

void Msg( const char* msg, ... )
{
    if ( msg == nullptr )
        return;
    using MsgFn = void(__cdecl*)( const char* msg, va_list );

    static auto fn = ( MsgFn )GetProcAddress( GetModuleHandleA( "tier0.dll" ), "Warning" );
    char buffer[ 4096 ];
    va_list list;
    va_start( list, msg );
    vsprintf_s( buffer, msg, list );
    va_end( list );
    fn( buffer, list );
}

class SendProp;
class SendTable;
class CSendProxyRecipients;
class RecvProp;
class DVariant;
class CSendTablePrecalc;

using ptr_t = void*;
using byte = unsigned char;
using CRC32_t = int;

using ArrayLengthSendProxyFn = int (*)( ptr_t pStruct, int objectID );
using SendVarProxyFn = void (*)( const SendProp* pProp, ptr_t pStructBase, ptr_t pData, DVariant* pOut, int iElement, int objectID );

using SendTableProxyFn = ptr_t(*)( const SendProp* pProp, ptr_t pStructBase, ptr_t pData, CSendProxyRecipients* pRecipients, int objectID );

using SendPropType = enum
{
    DPT_Int = 0,
    DPT_Float,
    DPT_Vector,
    DPT_VectorXY,
    // Only encodes the XY of a vector, ignores Z
    DPT_String,
    DPT_Array,
    // An array of the base types (can't be of datatables).
    DPT_DataTable,
    #if 0 // We can't ship this since it changes the size of DTVariant to be
    // 20 bytes instead of 16 and that breaks MODs!!!
    DPT_Quaternion,
    #endif
    DPT_Int64,
    DPT_NUMSendPropTypes
};

class DVariant
{
public:
    union
    {
        float m_Float;
        long m_Int;
        char* m_pString;
        ptr_t m_pData; // For DataTables.
        #if 0 // We can't ship this since it changes the size of DTVariant to be
      // 20 bytes instead of 16 and that breaks MODs!!!
        float   m_Vector[4];
        #else
        float m_Vector[ 3 ];
        #endif
        int64_t m_Int64;
    };

    SendPropType m_Type;
};

#define SPROP_UNSIGNED			(1<<0)	// Unsigned integer data.

#define SPROP_COORD				(1<<1)	// If this is set, the float/vector is treated like a world coordinate.
// Note that the bit count is ignored in this case.

#define SPROP_NOSCALE			(1<<2)	// For floating point, don't scale into range, just take value as is.

#define SPROP_ROUNDDOWN			(1<<3)	// For floating point, limit high value to range minus one bit unit

#define SPROP_ROUNDUP			(1<<4)	// For floating point, limit low value to range minus one bit unit

#define SPROP_NORMAL			(1<<5)	// If this is set, the vector is treated like a normal (only valid for vectors)

#define SPROP_EXCLUDE			(1<<6)	// This is an exclude prop (not excludED, but it points at another prop to be excluded).

#define SPROP_XYZE				(1<<7)	// Use XYZ/Exponent encoding for vectors.

#define SPROP_INSIDEARRAY		(1<<8)	// This tells us that the property is inside an array, so it shouldn't be put into the
// flattened property list. Its array will point at it when it needs to.

#define SPROP_PROXY_ALWAYS_YES	(1<<9)	// Set for datatable props using one of the default datatable proxies like
// SendProxy_DataTableToDataTable that always send the data to all clients.

#define SPROP_IS_A_VECTOR_ELEM	(1<<10)	// Set automatically if SPROP_VECTORELEM is used.

#define SPROP_COLLAPSIBLE		(1<<11)	// Set automatically if it's a datatable with an offset of 0 that doesn't change the pointer
// (ie: for all automatically-chained base classes).
// In this case, it can get rid of this SendPropDataTable altogether and spare the
// trouble of walking the hierarchy more than necessary.

#define SPROP_COORD_MP					(1<<12) // Like SPROP_COORD, but special handling for multiplayer games
#define SPROP_COORD_MP_LOWPRECISION 	(1<<13) // Like SPROP_COORD, but special handling for multiplayer games where the fractional component only gets a 3 bits instead of 5
#define SPROP_COORD_MP_INTEGRAL			(1<<14) // SPROP_COORD_MP, but coordinates are rounded to integral boundaries
#define SPROP_CELL_COORD				(1<<15) // Like SPROP_COORD, but special encoding for cell coordinates that can't be negative, bit count indicate maximum value
#define SPROP_CELL_COORD_LOWPRECISION 	(1<<16) // Like SPROP_CELL_COORD, but special handling where the fractional component only gets a 3 bits instead of 5
#define SPROP_CELL_COORD_INTEGRAL		(1<<17) // SPROP_CELL_COORD, but coordinates are rounded to integral boundaries

#define SPROP_CHANGES_OFTEN				(1<<18)	// this is an often changed field, moved to head of sendtable so it gets a small index

#define SPROP_VARINT					(1<<19)	// use var int encoded (google protobuf style), note you want to include SPROP_UNSIGNED if needed, its more efficient

#define SPROP_NUMFLAGBITS_NETWORKED		20

// This is server side only, it's used to mark properties whose SendProxy_* functions encode against gpGlobals->tickcount (the only ones that currently do this are
//  m_flAnimTime and m_flSimulationTime.  MODs shouldn't need to mess with this probably
#define SPROP_ENCODED_AGAINST_TICKCOUNT	(1<<20)

// See SPROP_NUMFLAGBITS_NETWORKED for the ones which are networked
#define SPROP_NUMFLAGBITS				21

#define SENDPROP_NETWORKVAR_FLAGS_SHIFT 20	// We store the networkvar flags in SendProp::m_Offset and this is what we shift it by.
#define SENDPROP_OFFSET_MASK			( ( 1 << SENDPROP_NETWORKVAR_FLAGS_SHIFT ) - 1 )

class SendProp
{
public:
    SendProp();
    virtual ~SendProp();

    void Clear();

    int GetOffset() const;
    void SetOffset( int i );

    SendVarProxyFn GetProxyFn() const;
    void SetProxyFn( SendVarProxyFn f );

    SendTableProxyFn GetDataTableProxyFn() const;
    void SetDataTableProxyFn( SendTableProxyFn f );

    SendTable* GetDataTable() const;
    void SetDataTable( SendTable* pTable );

    const char* GetExcludeDTName() const;

    // If it's one of the numbered "000", "001", etc properties in an array, then
    // these can be used to get its array property name for debugging.
    const char* GetParentArrayPropName() const;
    void SetParentArrayPropName( char* pArrayPropName );

    const char* GetName() const;

    bool IsSigned() const;

    bool IsExcludeProp() const;

    bool IsInsideArray() const; // Returns true if SPROP_INSIDEARRAY is set.
    void SetInsideArray();

    // Arrays only.
    void SetArrayProp( SendProp* pProp );
    SendProp* GetArrayProp() const;

    // Arrays only.
    void SetArrayLengthProxy( ArrayLengthSendProxyFn fn );
    ArrayLengthSendProxyFn GetArrayLengthProxy() const;

    int GetNumElements() const;
    void SetNumElements( int nElements );

    // Return the # of bits to encode an array length (must hold GetNumElements()).
    int GetNumArrayLengthBits() const;

    int GetElementStride() const;

    SendPropType GetType() const;

    int GetFlags() const;
    void SetFlags( int flags );

    // Some property types bind more data to the SendProp in here.
    const void* GetExtraData() const;
    void SetExtraData( const void* pData );

    byte GetPriority() const;
    void SetPriority( byte priority );

    // These tell us which kind of CNetworkVar we're referring to.
    int GetNetworkVarFlags() const;
    bool AreNetworkVarFlagsSet( int nFlags ) const;

    // This is only ever used by SendPropXXX functions internally. They receive offset with the networkvar
    // flags OR'd and shifted in, so they'll specify both at the same time.
    void SetOffsetAndNetworkVarFlags( int nOffsetAndFlags );

public:
    RecvProp* m_pMatchingRecvProp; // This is temporary and only used while precalculating
    // data for the decoders.

    SendPropType m_Type;
    int m_nBits;
    float m_fLowValue;
    float m_fHighValue;

    SendProp* m_pArrayProp; // If this is an array, this is the property that defines each array element.
    ArrayLengthSendProxyFn m_ArrayLengthProxy; // This callback returns the array length.

    int m_nElements; // Number of elements in the array (or 1 if it's not an array).
    int m_ElementStride; // Pointer distance between array elements.

    char* m_pExcludeDTName; // If this is an exclude prop, then this is the name of the datatable to exclude a prop from.
    char* m_pParentArrayPropName;

    char* m_pVarName;
    float m_fHighLowMul;

    byte m_priority;

    int m_Flags; // SPROP_ flags.

    SendVarProxyFn m_ProxyFn; // NULL for DPT_DataTable.
    SendTableProxyFn m_DataTableProxyFn; // Valid for DPT_DataTable.

    SendTable* m_pDataTable;

    // This also contains the NETWORKVAR_ flags shifted into the SENDPROP_NETWORKVAR_FLAGS_SHIFT range. 
    // (Use GetNetworkVarFlags to access them).
    int m_Offset;

    // Extra data bound to this property.
    const void* m_pExtraData;
};

inline int SendProp::GetOffset() const
{
    return m_Offset & SENDPROP_OFFSET_MASK;
}

inline void SendProp::SetOffset( int nOffset )
{
    m_Offset = nOffset;
}

inline void SendProp::SetOffsetAndNetworkVarFlags( int nOffset )
{
    m_Offset = nOffset;
}

inline int SendProp::GetNetworkVarFlags() const
{
    return m_Offset >> SENDPROP_NETWORKVAR_FLAGS_SHIFT;
}

inline bool SendProp::AreNetworkVarFlagsSet( int nFlags ) const
{
    return ( GetNetworkVarFlags() & nFlags ) == nFlags;
}

inline SendVarProxyFn SendProp::GetProxyFn() const
{
    return m_ProxyFn;
}

inline void SendProp::SetProxyFn( SendVarProxyFn f )
{
    m_ProxyFn = f;
}

inline SendTableProxyFn SendProp::GetDataTableProxyFn() const
{
    return m_DataTableProxyFn;
}

inline void SendProp::SetDataTableProxyFn( SendTableProxyFn f )
{
    m_DataTableProxyFn = f;
}

inline SendTable* SendProp::GetDataTable() const
{
    return m_pDataTable;
}

inline void SendProp::SetDataTable( SendTable* pTable )
{
    m_pDataTable = pTable;
}

inline const char* SendProp::GetExcludeDTName() const
{
    return m_pExcludeDTName;
}

inline const char* SendProp::GetParentArrayPropName() const
{
    return m_pParentArrayPropName;
}

inline void SendProp::SetParentArrayPropName( char* pArrayPropName )
{
    m_pParentArrayPropName = pArrayPropName;
}

inline const char* SendProp::GetName() const
{
    return m_pVarName;
}

inline bool SendProp::IsSigned() const
{
    return !( m_Flags & SPROP_UNSIGNED );
}

inline bool SendProp::IsExcludeProp() const
{
    return ( m_Flags & SPROP_EXCLUDE ) != 0;
}

inline bool SendProp::IsInsideArray() const
{
    return ( m_Flags & SPROP_INSIDEARRAY ) != 0;
}

inline void SendProp::SetInsideArray()
{
    m_Flags |= SPROP_INSIDEARRAY;
}

inline void SendProp::SetArrayProp( SendProp* pProp )
{
    m_pArrayProp = pProp;
}

inline SendProp* SendProp::GetArrayProp() const
{
    return m_pArrayProp;
}

inline void SendProp::SetArrayLengthProxy( ArrayLengthSendProxyFn fn )
{
    m_ArrayLengthProxy = fn;
}

inline ArrayLengthSendProxyFn SendProp::GetArrayLengthProxy() const
{
    return m_ArrayLengthProxy;
}

inline int SendProp::GetNumElements() const
{
    return m_nElements;
}

inline void SendProp::SetNumElements( int nElements )
{
    m_nElements = nElements;
}

inline int SendProp::GetElementStride() const
{
    return m_ElementStride;
}

inline SendPropType SendProp::GetType() const
{
    return m_Type;
}

inline int SendProp::GetFlags() const
{
    return m_Flags;
}

inline void SendProp::SetFlags( int flags )
{
    // Make sure they're using something from the valid set of flags.
    m_Flags = flags;
}

inline const void* SendProp::GetExtraData() const
{
    return m_pExtraData;
}

inline void SendProp::SetExtraData( const void* pData )
{
    m_pExtraData = pData;
}

inline byte SendProp::GetPriority() const
{
    return m_priority;
}

inline void SendProp::SetPriority( byte priority )
{
    m_priority = priority;
}

class SendTable
{
public:
    using PropType = SendProp;

    SendTable();
    SendTable( SendProp* pProps, int nProps, char* pNetTableName );
    ~SendTable();

    void Construct( SendProp* pProps, int nProps, char* pNetTableName );

    const char* GetName() const;

    int GetNumProps() const;
    SendProp* GetSendProp( int i );

    // Used by the engine.
    bool IsInitialized() const;
    void SetInitialized( bool bInitialized );

    // Used by the engine while writing info into the signon.
    void SetWriteFlag( bool bHasBeenWritten );
    bool GetWriteFlag() const;

    bool HasPropsEncodedAgainstTickCount() const;
    void SetHasPropsEncodedAgainstTickcount( bool bState );

public:
    SendProp* m_pProps;
    int m_nProps;

    char* m_pNetTableName; // The name matched between client and server.

    // The engine hooks the SendTable here.
    CSendTablePrecalc* m_pPrecalc;

protected:
    bool m_bInitialized : 1;
    bool m_bHasBeenWritten : 1;
    bool m_bHasPropsEncodedAgainstCurrentTickCount : 1; // m_flSimulationTime and m_flAnimTime, e.g.
};

inline const char* SendTable::GetName() const
{
    return m_pNetTableName;
}

inline int SendTable::GetNumProps() const
{
    return m_nProps;
}

inline SendProp* SendTable::GetSendProp( int i )
{
    return &m_pProps[ i ];
}

inline bool SendTable::IsInitialized() const
{
    return m_bInitialized;
}

inline void SendTable::SetInitialized( bool bInitialized )
{
    m_bInitialized = bInitialized;
}

inline bool SendTable::GetWriteFlag() const
{
    return m_bHasBeenWritten;
}

inline void SendTable::SetWriteFlag( bool bHasBeenWritten )
{
    m_bHasBeenWritten = bHasBeenWritten;
}

inline bool SendTable::HasPropsEncodedAgainstTickCount() const
{
    return m_bHasPropsEncodedAgainstCurrentTickCount;
}

inline void SendTable::SetHasPropsEncodedAgainstTickcount( bool bState )
{
    m_bHasPropsEncodedAgainstCurrentTickCount = bState;
}

struct ServerClass
{
    const char* m_pNetworkName;
    SendTable* m_pTable;
    ServerClass* m_pNext;
    int m_ClassID; // this is 0 when not in game.
    int m_InstanceBaselineIndex;
};

using fn = int(__fastcall)( void*, void* );
static fn* original = nullptr;

int __fastcall hk_GetNumPlayers( void* ecx, void* edx )
{
    if ( static auto address = find_pattern( "engine.dll", "50 8D 8D ? ? ? ? E8 ? ? ? ? 8B CF" ); reinterpret_cast<uintptr_t>( _ReturnAddress() ) == address )
        return 10;

    return original( ecx, edx );
}

inline unsigned int get_virtual( void* _class, unsigned int index )
{
    return static_cast<unsigned int>( ( *static_cast<int**>( _class ) )[ index ] );
}

extern void ProcessTable( SendTable* );

void ModifyTable()
{
    static auto g_SendTableCRC = *reinterpret_cast<int**>( find_pattern( "engine.dll", "A3 ? ? ? ? FF 15 ? ? ? ? 68 ? ? ? ? 8B C8 8B 10 FF 52 2C 85 C0 74 05" ) + 1 );
    *g_SendTableCRC = 1337;

    static auto g_pserverGameDLL = **reinterpret_cast<uintptr_t**>( find_pattern( "engine.dll", "8B 0D ? ? ? ? 83 C4 04 8B 01 FF 50 28 8B F8" ) + 2 );
    Msg( "%p\n", g_pserverGameDLL );

    static auto serverClass = reinterpret_cast<ServerClass * (__thiscall*)( void* )>( get_virtual( reinterpret_cast<void*>( g_pserverGameDLL ), 10 ) )( reinterpret_cast<void*>( g_pserverGameDLL ) );
    Msg( "%p\n", serverClass );

    while ( serverClass )
    {
        ProcessTable( serverClass->m_pTable );
        serverClass = serverClass->m_pNext;
    }
}

#define PROTOTYPE(name, type) \
    struct name { \
        using Fn_t = std::remove_pointer_t<type>; \
        static Fn_t Hooked; \
        static Fn_t* Original; \
    }; \
    inline name::Fn_t* name::Original;
PROTOTYPE(CreateBaseLine, void(__cdecl*)());


void __cdecl CreateBaseLine::Hooked()
{
    ModifyTable();
    Original();
}

void SendProxy_Int32ToInt32( const SendProp* pProp, const ptr_t pStruct, const ptr_t pData, DVariant* pOut, int iElement, int objectID )
{
    int data = *static_cast<int*>( pData );

    pOut->m_Int = ( data & 0xFFFFFFFF );
}

void ProcessTable( SendTable* table )
{
    // Msg( "TableName: %s | %p\n", table->m_pNetTableName, table );
    for ( auto i = 0; i < table->m_nProps; i++ )
    {
        auto prop = &table->m_pProps[ i ];

        if ( prop->GetDataTable() )
        {
            ProcessTable( prop->GetDataTable() );
        } else
        {
            if ( prop->m_Type == DPT_Float || prop->m_Type == DPT_Vector || prop->m_Type == DPT_VectorXY )
            {
                prop->SetFlags( SPROP_NOSCALE );
            }

            if ( strstr( prop->GetName(), "m_fFlags" ) )
            {
                prop->m_ProxyFn = ( SendProxy_Int32ToInt32 );
                prop->m_nBits = 32;
            }
        }

        // Msg( "Offset: %p, %s\n", prop->GetOffset(), prop->GetName() );
    }
}

DWORD WINAPI setup( LPVOID parameter )
{
    while ( !GetModuleHandleA( "engine.dll" ) || !GetModuleHandleA( "server.dll" ) )
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    auto GetPlayerNum = find_pattern( "engine.dll", "55 8B EC 51 53 8B D9 57 33 FF 39 BB" );
    auto CreateBaseLine = find_pattern( "engine.dll", "55 8B EC 83 EC 5C B9" );

    ModifyTable();
    Msg( "Finished\n" );

    try
    {
        if ( MH_Initialize() != MH_OK )
            throw std::runtime_error( "Initialize" );

        if ( MH_CreateHook( reinterpret_cast<LPVOID>( GetPlayerNum ), &hk_GetNumPlayers, reinterpret_cast<LPVOID*>( &original ) ) != MH_OK )
            throw std::runtime_error( "CreateHook" );

        if ( MH_CreateHook( reinterpret_cast<LPVOID>( CreateBaseLine ), &CreateBaseLine::Hooked, reinterpret_cast<LPVOID*>( &CreateBaseLine::Original) ) != MH_OK )
            throw std::runtime_error( "CreateHook" );

        if ( MH_EnableHook( nullptr ) != MH_OK )
            throw std::runtime_error( "EnableHook" );
        Msg( "[+] Enabled hook\n" );
    }
    catch ( std::exception& ex )
    {
        Msg( "[x] %s\n", ex.what() );
        FreeLibraryAndExitThread( static_cast<HMODULE>( parameter ), EXIT_SUCCESS );
    }
    return TRUE;
}

DWORD WINAPI destroy( LPVOID parameter )
{
    while ( !GetAsyncKeyState( VK_F10 ) )
        std::this_thread::sleep_for( std::chrono::milliseconds( 500 ) );

    MH_Uninitialize();
    MH_DisableHook( nullptr );
    Msg( "Bye\n" );
    FreeLibraryAndExitThread( static_cast<HMODULE>( parameter ), EXIT_SUCCESS );
}

BOOL APIENTRY DllMain( HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved )
{
    if ( ul_reason_for_call == DLL_PROCESS_ATTACH )
    {
        DisableThreadLibraryCalls( hModule );
        if ( const auto thread = CreateThread( nullptr, 0, setup, hModule, 0UL, nullptr ); thread != nullptr )
            CloseHandle( thread );

        if ( const auto thread = CreateThread( nullptr, 0, destroy, hModule, 0UL, nullptr ); thread != nullptr )
            CloseHandle( thread );
    }
    return TRUE;
}
