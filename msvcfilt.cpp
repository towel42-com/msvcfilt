#include <iostream>
#include <memory>
#include <regex>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <cstring>
#include <list>
#include <optional>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <DbgHelp.h>

//------------------------------------------------------------------------------

/**
 * This class interacts with the DbgHelp symbol handler functions.
 *
 * It is designed as a Meyers' Singleton, so that SymInitialize() will only
 * be called if it is actually necessary, and that SymCleanup() will be called
 * at the end of the program.
 */
class SymbolHandler
{
    using TBufferType = std::unique_ptr< char[] >;

    /// Default constructor
    SymbolHandler()
    {
        fUndecoratedBuffer = TBufferType( new char[ kMAX_SYMBOL_NAME_LEN + 1 ] );
        fProc = GetCurrentProcess();
        if ( !SymInitialize( fProc, nullptr, false ) )
            fProc = nullptr;
    }

    /// Destructor
    ~SymbolHandler()
    {
        if ( fProc )
        {
            SymCleanup( fProc );
        }
    }

public:
    /// The maximum length of a symbol name in bytes
    static const size_t kMAX_SYMBOL_NAME_LEN = MAX_SYM_NAME;

    SymbolHandler( const SymbolHandler & ) = delete;
    SymbolHandler &operator=( const SymbolHandler & ) = delete;

    std::optional< std::string > UndecorateSymbol( const std::string_view &symbol )
    {
        auto res = UnDecorateSymbolName( std::string( symbol ).c_str(), fUndecoratedBuffer.get(), kMAX_SYMBOL_NAME_LEN, UNDNAME_COMPLETE );

        bool success = ( res != 0 );
        if ( success )
        {
            return fUndecoratedBuffer.get();
        }

        return {};
    }

    /// Returns the singleton instance of the class
    static SymbolHandler &GetInstance()
    {
        static SymbolHandler instance;
        return instance;
    }

private:
    /// True if the instance was successfully initialized
    /// Windows handle for the current process
    HANDLE fProc{ nullptr };
    /// Internal buffer that receives the undecorated symbols
    TBufferType fUndecoratedBuffer;
};

void showHelp()
{
    std::cout   //
        << "Usage: msvcfilt [OPTIONS] <decorated string>..." << std::endl
        << "Searches input stream for Microsoft Visual C++ decorated symbol names" << std::endl
        << "and replaces them with their undecorated equivalent." << std::endl
        << std::endl
        << "Options:" << std::endl
        << "    -help, --help    Display this help and exit." << std::endl
        << "    -keep, --keep    Does not replace the original, decorated symbol name." << std::endl
        << "                     Instead, the undecorated name will be inserted after it." << std::endl
        << "    Uses STDIN rather than <decorated string> if not set" << std::endl;

    return;
}

bool gKeepOldName = false;
std::optional< std::list< std::string > > gInputStrings;

bool processArgs( int argc, char **argv )
{
    for ( int idx = 1; idx < argc; ++idx )
    {
        if ( strcmp( "-help", argv[ idx ] ) == 0 || strcmp( "--help", argv[ idx ] ) == 0 )
        {
            showHelp();
            return false;
        }
        else if ( std::strcmp( "-keep", argv[ idx ] ) == 0 || std::strcmp( "--keep", argv[ idx ] ) == 0 )
        {
            gKeepOldName = true;
        }
        else
        {
            if ( !gInputStrings.has_value() )
                gInputStrings = std::list< std::string >();
            gInputStrings.value().emplace_back( argv[ idx ] );
        }
    }
    return true;
}

//------------------------------------------------------------------------------

static bool inputValid()
{
    if ( !gInputStrings.has_value() )
        return std::cin.good();
    else
        return !gInputStrings.value().empty();
}

static std::optional< std::string > getNextLine()
{
    if ( !inputValid() )
        return {};

    std::string line;
    if ( !gInputStrings.has_value() )
    {
        std::getline( std::cin, line );
    }
    else
    {
        if ( !gInputStrings.value().empty() )
        {
            line = gInputStrings.value().front();
            gInputStrings.value().pop_front();
        }
    }
    return line;
}

int main( int argc, char **argv )
{
    if ( !processArgs( argc, argv ) )
        return 0;

    /// The regex pattern to recognize a decorated symbol. Only a guess.
    static const char *DECORATED_SYMBOL_PATTERN = R"__(\?[a-zA-Z0-9_@?$]+)__";

    // instantiate the regex pattern to search for
    std::regex pattern( DECORATED_SYMBOL_PATTERN );

    std::optional< std::string > line;

    while ( ( line = getNextLine() ).has_value() )
    {
        // find every match
        // print out data from end of previous match, till the start of the current one
        // print out the undecorated string
        // continue as long as there is a match
        // print out the remainder
        auto it = std::sregex_token_iterator( line.value().begin(), line.value().end(), pattern );
        const std::sregex_token_iterator end;

        std::string::const_iterator prevPos = line.value().begin();
        for ( ; it != end; ++it )
        {
            auto startPos = ( *it ).first;
            auto endPos = ( *it ).second;

            std::cout << std::string_view( prevPos, startPos );
            prevPos = endPos;

            auto symbol = std::string_view( startPos, endPos );

            auto result = SymbolHandler::GetInstance().UndecorateSymbol( symbol );
            if ( !result.has_value() )
            {
                continue;
            }

            if ( gKeepOldName )
            {
                std::cout << ( *it ).str() << R"__( ")__" + result.value() + R"__(")__";
            }
            else
            {
                std::cout << result.value();
            }
        }

        if ( prevPos != line.value().end() )
        {
            std::cout << &( *prevPos );
        }
        std::cout << std::endl;
    }
}
