
#if !defined(FDF_USE_CPP_MODULES)
    #include <cstdint>
    #include <type_traits>
    #include <string>
    #include <vector>
    #include <format>
    #include <filesystem>
    #include <fstream>
    #include <cctype>
    #include <span>
    #include <utility>
    #include <algorithm>

    #define FDF_EXPORT
#endif




// TODO: Maybe add an option to lazily evaluate? (store every value as a string and don't process anything until requested)
// TODO: Maybe add some kind of enum type (to file format)
// TODO: Add API to read/modify data
// TODO: Allow multidimensional bool
// TODO: add a way to access child Entries from parent




FDF_EXPORT namespace fdf
{
    enum class Type : uint8_t
    {
        Invalid,
        Null,

        Bool,
        Int,
        UInt,
        Float,

        String,
        Hex,
        Version,
        Timestamp,

        Array,
        Map
    };


    struct Style
    {
        consteval Style() noexcept = default;

    public:
        uint8_t singleLineCommentLimit = 80;
        uint8_t tabSize = 4;
        bool bUseSpacesOverTabs = true;
        bool bParanthesisOnNewLine = true;
        bool bCommasOnArrays = true;
        bool bCommasOnObjects = true;
        bool bCommasOnLastElement = true;
        bool bSingleLineForShortArrays = true;
        bool bSingleLineForShortObjects = true;
        bool bSpaceWithinParanthesis = true;
        bool bSpaceBeforeAndAfterEqualSign = false;
        bool bGroupSimilarTypes = true;
        bool bUppercaseHex = true;
        bool bEmptyLineAtTheEndOfTheFile = true;
        bool bAlignCloseComments = true;
        bool bUseEqualSignForSingleLineArraysAndObjects = false;
    };




    enum class CommentCombineStrategy : uint8_t
    {
        UseExisting,
        UseNew,
        UseNewIfExistingIsEmpty,
        Merge,
        Clear
    };

    enum class Error : uint8_t
    {
        AlreadyHasComment,
        Warning_Begin = AlreadyHasComment,
        Warning_End = Warning_Begin,

        UnexpectedToken,
        Error_Begin = UnexpectedToken,
        Error_End = Error_Begin,
    };

    constexpr bool IsWarning(Error type) noexcept
    {
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(Error::Warning_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(Error::Warning_End);
    }
    constexpr bool IsError(Error type) noexcept
    {
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(Error::Error_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(Error::Error_End);
    }

    class Entry;
}




namespace fdf::detail
{
    constexpr std::string_view EVALUATE_LITERAL_TEXT = "Evaluate Literal";
    constexpr std::string_view NONE_TEXT  = "<NONE>";
    constexpr std::string_view NULL_TEXT  = "<NULL>";
    constexpr std::string_view TRUE_TEXT  = "<TRUE>";
    constexpr std::string_view FALSE_TEXT = "<FALSE>";
    constexpr std::string_view ARRAY_TEXT = "<ARRAY>";
    constexpr std::string_view MAP_TEXT   = "<MAP>";

    constexpr uint64_t  INT64_MAX_VALUE = std::numeric_limits< int64_t>::max();
    constexpr uint64_t UINT64_MAX_VALUE = std::numeric_limits<uint64_t>::max();
    constexpr double   DOUBLE_MAX_VALUE = std::numeric_limits<  double>::max();

    constexpr size_t VARIANT_SIZE = 5 * sizeof(int64_t);  // 5x 64 bit value
    constexpr size_t VARIANT_32BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int32_t);
    constexpr size_t VARIANT_64BIT_ELEMENT_COUNT = VARIANT_SIZE / sizeof(int64_t);
    constexpr size_t VARIANT_DYNAMIC_STRING_HARD_LIMIT = (VARIANT_SIZE * 2.5);


    constexpr std::string_view KEYWORDS[] =
    {
        "null",
        "true",
        "false",
    };
    constexpr size_t KEYWORD_COUNT = sizeof(KEYWORDS) / sizeof(KEYWORDS[0]);

    enum class TokenType : uint8_t
    {
        NonExisting,  // Means the requested token doesn't exist/cannot be accessed, not necessarily an error
        Invalid,      // Means there is an error in the file content
        NewLine,
        EndOfFile,
        Comment,

        Equal,
        Comma,

        CurlyBraceOpen,
        CurlyBraceClose,
        SquareBraceOpen,
        SquareBraceClose,

        Identifier,

        Keyword,
        ValueLiteral_Begin = Keyword,
        IntLiteral,
        FloatLiteral,
        StringLiteral,
        HexLiteral,
        VersionLiteral,
        TimestampLiteral,

        EvaluateLiteral,
        ValueLiteral_End = EvaluateLiteral,
    };

    struct Token
    {
        constexpr Token() noexcept = default;
        constexpr Token(TokenType type_, uint32_t startPosition_ = 0, size_t count_ = 0)
            : type(type_), count(count_), startPosition(startPosition_)  { }

        constexpr std::string_view ToView(std::string_view buffer) const noexcept  { return buffer.substr(startPosition, count); }

        TokenType type = TokenType::NonExisting;
        uint8_t  extra8  = 0; // Token specific data, if needed
        uint16_t extra16 = 0; // Token specific data, if needed
        uint32_t count = 0;
        size_t startPosition = 0;
        uint32_t line = 0;
        uint32_t column = 0;
    };

    struct Tokenizer
    {
        constexpr Tokenizer(std::string_view content_) noexcept
            : content(content_), index(0), line(1), lastNewLineIndex(0), currentToken(GetNextToken())  { }

        constexpr Token Current() const noexcept  { return currentToken; }
        constexpr Token Advance()       noexcept  { currentToken = GetNextToken(); return currentToken; }

    private:
        constexpr Token GetNextToken() noexcept;

    private:
        std::string_view content;
        size_t index;
        size_t line;
        size_t lastNewLineIndex;
        Token currentToken;
    };
}




namespace fdf::detail
{
    struct Test;

    template<auto ERROR_CALLBACK>
    struct Utils;

    template <typename Callable>
    constexpr bool IsValidErrorCallback = std::is_invocable_r_v<bool, Callable, Error, std::string_view>;
    inline constexpr auto DefaultErrorCallback = [](Error error, std::string_view message) -> bool  { return true; };


    constexpr void constexpr_memcpy(char* dest, const char* src, size_t size)
    {
        for(size_t i = 0; i < size; i++)
            dest[i] = src[i];
    }

    constexpr bool IsValueLiteral(TokenType type) noexcept
    { 
        return static_cast<uint8_t>(type) >= static_cast<uint8_t>(TokenType::ValueLiteral_Begin) &&
               static_cast<uint8_t>(type) <= static_cast<uint8_t>(TokenType::ValueLiteral_End);
    }

    constexpr void TrimWhitespaceMultilineInPlace(std::string_view view, std::string& out)
    {
        out.clear();
        out.reserve(view.size());

        bool bAfterNewLine = true;
        for(char c : view)
        {
            if(bAfterNewLine)
            {
                if(std::isspace(c))
                    continue;

                bAfterNewLine = false;
                out.push_back(c);
            }
            else
            {
                out.push_back(c);
                if(c == '\n')
                    bAfterNewLine = true;
            }
        }
    }
    constexpr std::string TrimWhitespaceMultiline(std::string_view view)
    {
        if(view.empty())
            return {};

        std::string temp;
        TrimWhitespaceMultilineInPlace(view, temp);
        return temp;
    }
}




















namespace fdf::detail
{
    union Variant
    {
        bool     b[VARIANT_SIZE];
        int64_t  i[VARIANT_64BIT_ELEMENT_COUNT];
        uint64_t u[VARIANT_64BIT_ELEMENT_COUNT];
        double   f[VARIANT_64BIT_ELEMENT_COUNT];
        char   str[VARIANT_SIZE];

        struct String
        {
            constexpr char& operator[](size_t i)       noexcept  { return data[i]; }
            constexpr char  operator[](size_t i) const noexcept  { return data[i]; }

            constexpr void Delete() noexcept
            {
                delete[] data;
                capacity = 0;
            }
            constexpr void Release() noexcept
            {
                data = nullptr;
                capacity = 0;
            }
            constexpr void Reserve(size_t newCapacity)
            {
                if(newCapacity <= capacity)
                    return;

                char* newData = new char[newCapacity];
                if(data != nullptr)
                {
                    constexpr_memcpy(newData, data, capacity);
                    delete[] data;
                }
                data = newData;
                capacity = newCapacity;
                RefreshView();
            }
            constexpr void InitialAllocate(size_t newCapacity)
            {
                data = new char[newCapacity];
                capacity = newCapacity;
                RefreshView();
            }
            constexpr void Reallocate(size_t newCapacity)
            {
                size_t smallest = newCapacity > capacity? capacity : newCapacity;
                char* newData = new char[capacity];

                constexpr_memcpy(newData, data, smallest);
                delete[] data;

                data = newData;
                capacity = newCapacity;
                RefreshView();
            }
            constexpr String Copy() const
            {
                String other{};
                if(capacity > 0 && data != nullptr)
                {
                    other.InitialAllocate(capacity);
                    constexpr_memcpy(other.data, data, capacity);
                    other.RefreshView();
                }

                return other;
            }
            constexpr String Move() noexcept
            {
                String other = *this;
                *this = {};
                return other;
            }
            constexpr void RefreshView() noexcept
            {
                view = std::string_view(data, capacity);
            }

            size_t capacity;
            char* data;
            std::string_view view;
        } strDynamic;

    public:
        constexpr  Variant() noexcept  { }
        constexpr ~Variant() noexcept  { }
    };
}




FDF_EXPORT namespace fdf
{
    template<auto ERROR_CALLBACK = detail::DefaultErrorCallback> requires(detail::IsValidErrorCallback<decltype(ERROR_CALLBACK)>)
    class IO;

    class Entry
    {
        friend struct detail::Test;

        template<auto ERROR_CALLBACK>
        friend struct detail::Utils;

        template<auto ERROR_CALLBACK> requires(detail::IsValidErrorCallback<decltype(ERROR_CALLBACK)>)
        friend class IO;

        static Entry INVALID;

    private:
        Type type = Type::Invalid;
        uint8_t depth = 0;  // Depth of the entry (0 for top level, 1 for child of top level, 2 for grandchild of top level, ...)
        uint8_t identifierSize = 0;
        uint32_t size = 0;  // If Array or Map this is count of top level childs, otherwise type specific (for example: character count for string)

        detail::Variant data;
        std::string fullIdentifier;

    public:
        std::string comment;

    public:
        constexpr Entry() noexcept = default;
        constexpr ~Entry() noexcept
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                data.strDynamic.Delete();
        }


        constexpr Entry(const Entry& other)
            : type(other.type), depth(other.depth), identifierSize(other.identifierSize), size(other.size), fullIdentifier(other.fullIdentifier), comment(other.comment)
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                data.strDynamic = other.data.strDynamic.Copy();
            else
                data = other.data;
        }
        constexpr Entry(Entry&& other) noexcept
            : type(other.type), depth(other.depth), identifierSize(other.identifierSize), size(other.size), fullIdentifier(std::move(other.fullIdentifier)), comment(std::move(other.comment))
        {
            if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                data.strDynamic = other.data.strDynamic.Move();
            else
                data = other.data;

            other.type = Type::Invalid;
        }


        constexpr Entry& operator=(const Entry& other)
        {
            if(this != &other)
            {
                type = other.type;
                depth = other.depth;
                identifierSize = other.identifierSize;
                size = other.size;
                fullIdentifier = other.fullIdentifier;
                comment = other.comment;

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                    data.strDynamic.Delete();

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                    data.strDynamic = other.data.strDynamic.Copy();
                else
                    data = other.data;
            }
            return *this;
        }
        constexpr Entry& operator=(Entry&& other) noexcept
        {
            if(this != &other)
            {
                type = other.type;
                depth = other.depth;
                identifierSize = other.identifierSize;
                size = other.size;
                fullIdentifier = std::move(other.fullIdentifier);
                comment = std::move(other.comment);

                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                    data.strDynamic.Delete();
                
                if((type == Type::String || type == Type::Hex || type == Type::Timestamp) && size > detail::VARIANT_SIZE - 1)
                    data.strDynamic = other.data.strDynamic.Move();
                else
                    data = other.data;

                other.type = Type::Invalid;
            }
            return *this;
        }

    public:
        constexpr uint32_t GetChildCount() const noexcept  { return IsContainer()? size : 0; }
        constexpr uint8_t  GetDepth()      const noexcept  { return depth; }
        constexpr Type     GetType()       const noexcept  { return type; }
        constexpr bool     IsValid()       const noexcept  { return type != Type::Invalid; }
        constexpr bool     IsNull()        const noexcept  { return type == Type::Null; }
        constexpr bool     IsContainer()   const noexcept  { return type == Type::Array || type == Type::Map; }
        constexpr bool     HasValue()      const noexcept  { return IsValid() && !IsNull() && !IsContainer(); }


        constexpr std::string_view GetFullIdentifier() const noexcept
        {
            return fullIdentifier;
        }
        constexpr std::string_view GetIdentifier() const noexcept
        {
            return std::string_view(fullIdentifier.data() + fullIdentifier.size() - identifierSize, identifierSize);
        }
        constexpr std::string_view GetIdentifierWithDot() const noexcept
        {
            return std::string_view(fullIdentifier.data() + fullIdentifier.size() - identifierSize - 1, identifierSize + 1);
        }
        constexpr std::string_view GetParentIdentifier() const noexcept
        {
            return std::string_view(fullIdentifier.data(), fullIdentifier.size() - identifierSize - 1);
        }
        constexpr std::string_view GetParentIdentifierWithDot() const noexcept
        {
            return std::string_view(fullIdentifier.data(), fullIdentifier.size() - identifierSize);
        }

        constexpr void SetIdentifier(std::string_view newIdentifier)
        {
            if(newIdentifier.empty())
                throw std::runtime_error("Identifier can't be empty");

            fullIdentifier.resize(fullIdentifier.size() - identifierSize);
            fullIdentifier += newIdentifier;
            identifierSize = newIdentifier.size();
        }


        constexpr std::string_view DataToView(std::string& temp) const
        {
            switch(type)
            {
                case Type::Invalid: return detail::NONE_TEXT;
                case Type::Null:    return detail::NULL_TEXT;
                case Type::Array:   return detail::ARRAY_TEXT;
                case Type::Map:     return detail::MAP_TEXT;
                case Type::Bool:    return data.b[0]? detail::TRUE_TEXT : detail::FALSE_TEXT;

                case Type::String:
                case Type::Hex:
                case Type::Timestamp:
                    return size > detail::VARIANT_SIZE - 1? data.strDynamic.view.substr(0, size) : std::string_view(data.str, size);

                case Type::Version:
                    temp = std::format("{}.{}.{}.{}", data.u[0], data.u[1], data.u[2], data.u[3]);
                    return temp;

                case Type::Int:
                    temp = std::format("{}", data.i[0]);
                    for(int i = 1; i < size; i++)
                        temp = std::format("{}x{}", temp, data.i[i]);
                    return temp;

                case Type::UInt:
                    temp = std::format("{}", data.u[0]);
                    for(int i = 1; i < size; i++)
                        temp = std::format("{}x{}", temp, data.u[i]);
                    return temp;

                case Type::Float:
                    temp = std::format("{}", data.f[0]);
                    for(int i = 1; i < size; i++)
                        temp = std::format("{}x{}", temp, data.f[i]);
                    return temp;

                default:
                    std::unreachable();
                    return {};
            }
        };




        template<typename T>
        constexpr auto GetValue() const  { }

        template<>
        constexpr auto GetValue<bool>() const
        {
            if(type != Type::Bool)
                throw std::runtime_error("Non matching type is not 'bool'");
            return data.b[0];
        }

        template<>
        constexpr auto GetValue<int64_t>() const
        {
            if(type != Type::Int)
                throw std::runtime_error("Non matching type is not 'int64_t'");
            return std::span<const int64_t>(data.i, size);
        }
        template<>
        constexpr auto GetValue<int>() const  { return GetValue<int64_t>(); }

        template<>
        constexpr auto GetValue<uint64_t>() const
        {
            if(type != Type::UInt && type != Type::Version)
                throw std::runtime_error("Non matching type is not 'uint64_t'");
            return std::span<const uint64_t>(data.u, size);
        }
        template<>
        constexpr auto GetValue<unsigned int>() const  { return GetValue<uint64_t>(); }

        template<>
        constexpr auto GetValue<double>() const
        {
            if(type != Type::Float)
                throw std::runtime_error("Non matching type is not 'double'");
            return std::span<const double>(data.f, size);
        }
        template<>
        constexpr auto GetValue<float>() const  { return GetValue<double>(); }

        template<>
        constexpr auto GetValue<char>() const
        {
            if(type != Type::String && type != Type::Hex && type != Type::Timestamp)
                throw std::runtime_error("Non matching type is not 'string'");

            if(size > detail::VARIANT_SIZE - 1)
                return std::string_view(data.strDynamic.data, size);
            return std::string_view(data.str, size);
        }
        template<>
        constexpr auto GetValue<std::string>() const  { return GetValue<char>(); }
        template<>
        constexpr auto GetValue<std::string_view>() const  { return GetValue<char>(); }




        template<typename T>
        constexpr auto GetValueUnsafe() const  { }

        template<>
        constexpr auto GetValueUnsafe<bool>() const
        {
            return data.b[0];
        }

        template<>
        constexpr auto GetValueUnsafe<int64_t>() const
        {
            return std::span<const int64_t>(data.i, size);
        }
        template<>
        constexpr auto GetValueUnsafe<int>() const  { return GetValueUnsafe<int64_t>(); }

        template<>
        constexpr auto GetValueUnsafe<uint64_t>() const
        {
            return std::span<const uint64_t>(data.u, size);
        }
        template<>
        constexpr auto GetValueUnsafe<unsigned int>() const  { return GetValueUnsafe<uint64_t>(); }

        template<>
        constexpr auto GetValueUnsafe<double>() const
        {
            return std::span<const double>(data.f, size);
        }
        template<>
        constexpr auto GetValueUnsafe<float>() const  { return GetValueUnsafe<double>(); }

        template<>
        constexpr auto GetValueUnsafe<char>() const
        {
            if(size > detail::VARIANT_SIZE - 1)
                return std::string_view(data.strDynamic.data, size);
            return std::string_view(data.str, size);
        }
        template<>
        constexpr auto GetValueUnsafe<std::string>() const  { return GetValueUnsafe<char>(); }
        template<>
        constexpr auto GetValueUnsafe<std::string_view>() const  { return GetValueUnsafe<char>(); }
    };
}

inline fdf::Entry fdf::Entry::INVALID;










namespace fdf::detail
{
    #define CHECK_TOKEN(TOKEN)         if(TOKEN.type == TokenType::Invalid  ) return false
    #define CHECK_TOKEN_FOR_EOF(TOKEN) if(TOKEN.type == TokenType::EndOfFile) return false




    constexpr Token Tokenizer::GetNextToken() noexcept
    {
        if(index >= content.size())
            return TokenType::EndOfFile;

        while(std::isspace(content[index]))
        {
            if(content[index] == '\n')
            {
                Token token = Token(TokenType::NewLine, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                while(index < content.size() && std::isspace(content[index]))
                {
                    if(content[index] == '\n')
                    {
                        line++;
                        lastNewLineIndex = index;
                    }


                    index++;
                    token.count++;
                }

                return token;
            }

            if(std::isspace(content[index]))
                index++;

            if(index >= content.size())
                return TokenType::EndOfFile;
        }



        if(content[index] == '\"' || content[index] == '\'')
        {
            size_t nextQuote = content.find_first_of(content[index], index + 1);
            if(nextQuote == std::string_view::npos)
                return TokenType::Invalid;  // Non matching quotes

            while(content[nextQuote - 1] == '\\')
            {
                nextQuote = content.find_first_of(content[index], nextQuote + 1);
                if(nextQuote == std::string_view::npos)
                    return TokenType::Invalid;  // Non matching quotes
            }

            Token token = Token(TokenType::StringLiteral, index, nextQuote + 1 - index);
            index = nextQuote + 1;
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;
            return token;
        }



        if(content[index] == '/')
        {
            if(index + 2 >= content.size())
                return TokenType::Invalid; // not enough space for a comment

            if(content[index + 1] == '/') // single line comment
            {
                size_t newLinePos = content.find_first_of('\n', index + 2);
                Token token = Token(TokenType::Comment, content[index + 2] == ' '? index + 3 : index + 2);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;

                if(newLinePos != std::string_view::npos)
                {
                    token.count = newLinePos - token.startPosition;
                    index = newLinePos;
                    return token;
                }

                // There is no new lines left (comment is at the end of the file)
                token.count = content.size() - token.startPosition;
                index = -1;
                return token;
            }

            if(content[index + 1] == '*') // multi line comment
            {
                size_t slashPos = content.find_first_of('/', index + 2);
                while(true)
                {
                    if(slashPos == std::string_view::npos)
                        return TokenType::Invalid; // Non matching comment scope (There is only "/*" and not "*/")

                    if(content[slashPos - 1] == '*')
                    {
                        Token token = Token(TokenType::Comment, index + 2);
                        token.line = line;
                        token.column = token.startPosition - lastNewLineIndex;
                        token.extra8 = 1;  // Means multi line
                        token.count = slashPos - 2 - token.startPosition;

                        for(size_t i = index + 2; i < slashPos - 1; i++)
                        {
                            if(content[i] == '\n')
                                line++;
                        }

                        index = slashPos + 1;
                        if(index + 1 < content.size() && content[index] == '\n')
                        {
                            lastNewLineIndex = index;
                            line++;
                            index++;
                        }

                        if(token.count == -1)
                            token.count = 0;
                        return token;
                    }

                    slashPos = content.find_first_of('/', slashPos + 2);
                }
            }

            return TokenType::Invalid;  // slash "/" without a comment
        }



        if(content[index] == '{')
            return Token(TokenType::CurlyBraceOpen, index++, 1);
        if(content[index] == '}')
            return Token(TokenType::CurlyBraceClose, index++, 1);
        if(content[index] == '[')
            return Token(TokenType::SquareBraceOpen, index++, 1);
        if(content[index] == ']')
            return Token(TokenType::SquareBraceClose, index++, 1);

        if(content[index] == '=')
            return Token(TokenType::Equal, index++, 1);
        if(content[index] == ',')
            return Token(TokenType::Comma, index++, 1);



        if(content[index] == '$')
        {
            if(index + 1 < content.size() && content[index + 1] == '{')
            {
                size_t braceClose = content.find_first_of('}', index + 2);
                if(braceClose == std::string_view::npos) // we reached eof before "}"
                    return TokenType::Invalid;

                Token token = Token(TokenType::EvaluateLiteral, index, braceClose + 1 - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                index = braceClose + 1;
                return token;
            }

            return TokenType::Invalid; // Random "$" without "{"
        }



        if(std::isalpha(content[index]) || content[index] == '_') // identifier, keyword
        {
            Token token = Token(TokenType::Identifier, index);
            token.line = line;
            token.column = token.startPosition - lastNewLineIndex;
            auto checkKeywords = [&](std::string_view view) -> void
            {
                for(size_t i = 0; i < KEYWORD_COUNT; i++)
                {
                    if(view == KEYWORDS[i])
                    {
                        token.type = TokenType::Keyword;
                        token.extra8 = i;  // Used as keyword index
                        return;
                    }
                }
            };

            size_t firstNonAlpha = index + 1;
            while(firstNonAlpha < content.size() && (std::isalpha(content[firstNonAlpha]) || std::isdigit(content[firstNonAlpha]) || content[firstNonAlpha] == '_'))
                firstNonAlpha++;

            token.count = firstNonAlpha - token.startPosition;
            std::string_view view = token.ToView(content);

            if(firstNonAlpha >= content.size()) // we reached eof before any space or any other token
            {
                checkKeywords(view);
                index = -1;
                return token;
            }

            checkKeywords(view);
            index = firstNonAlpha;
            return token;
        }



        if(std::isdigit(content[index]) || content[index] == '-')
        {
            if(content[index] == '0' && index + 3 < content.size() && content[index + 1] == 'x')  // Hex
            {
                size_t firstNonHex = content.find_first_not_of("0123456789abcdefABCDEF", index + 2);
                size_t firstChar = content.find_first_of("abcdefABCDEF", index + 2);
                size_t firstHash = content.find_first_of('#', index + 2);
                if(firstNonHex == firstHash && firstNonHex != std::string_view::npos) // First non hex character is "#"
                {
                    Token token = Token(TokenType::HexLiteral, index, firstNonHex - index);
                    token.line = line;
                    token.column = token.startPosition - lastNewLineIndex;
                    index = firstNonHex + 1;
                    return token;
                }

                if(firstNonHex == std::string_view::npos) // we reached eof before any space or any other token
                    return TokenType::Invalid;

                if(firstChar < firstNonHex) // it contains hex characters, so we can't let it slide as a number
                    return TokenType::Invalid;

                // Let it fallthrough as "multi dimensional int"
            }



            size_t firstNonDigit = content.find_first_not_of("0123456789", index + 1);
            if(firstNonDigit == std::string_view::npos)  // we reached eof before any space or any other token
            {
                Token token = Token(TokenType::IntLiteral, index, content.size() - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                index = -1;
                return token;
            }

            if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
            {
                Token token = Token(TokenType::IntLiteral, index, firstNonDigit - index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)
                index = firstNonDigit;
                return token;
            }

            if(content[firstNonDigit] == '.')  // float, version or multi dimensional float
            {
                Token token = Token(TokenType::FloatLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 1;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                size_t dotCount = 0;
                size_t temp = firstNonDigit;
                char lastChar = '.';
                bool bContainsDash = false;

                auto calculateResult = [&]() -> void
                {
                    if(lastChar == '.' || lastChar == 'x')
                    {
                        token.type = TokenType::Invalid;  // Must end with a digit
                        return;
                    }

                    token.count = temp - token.startPosition;

                    if(dotCount == 2 || dotCount == 3)
                    {
                        if(bContainsDash)
                        {
                            token.type = TokenType::Invalid;  // Version cannot contain dash
                            return;
                        }

                        token.type = TokenType::VersionLiteral;
                        token.extra8 = dotCount + 1;
                        return;
                    }
                };

                while(temp <= content.size())
                {
                    lastChar = content[temp];
                    if(std::isdigit(content[temp]) || (content[temp] == '-' && lastChar == 'x'))
                    {
                        if(content[temp] == '-')
                            bContainsDash = true;

                        temp++;
                        continue;
                    }

                    if(content[temp] == '.')
                    {
                        if(dotCount == 1 && token.extra8 > 1)
                            return TokenType::Invalid;  // Float can't have more than 1 dot
                        if(dotCount > 2)
                            return TokenType::Invalid;  // Version can have 3 dots maximum
                        
                        dotCount++;
                        temp++;
                        continue;
                    }

                    if(content[temp] == 'x')
                    {
                        dotCount = 0;
                        token.extra8++;
                        temp++;
                        continue;
                    }

                    if(std::isspace(content[temp]) || content[temp] == ',')
                    {
                        calculateResult();
                        index = temp;
                        return token;
                    }

                    return TokenType::Invalid; // Non allowed character
                }

                calculateResult();
                index = -1;
                return token;
            }

            if(content[firstNonDigit] == 'x')  // multi dimensional int
            {
                Token token = Token(TokenType::IntLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                token.extra8 = 2;  // Used as dimension (2d, 3d, 4d, 5d, etc)

                size_t dotCount = 0;
                while(true)
                {
                    size_t previous = firstNonDigit;
                    firstNonDigit = content.find_first_not_of("0123456789", firstNonDigit + 1);

                    if(firstNonDigit == std::string_view::npos) // we reached eof before any space or any other token
                    {
                        token.count = content.size() - token.startPosition;
                        index = -1;
                        return token;
                    }

                    if(previous + 1 == firstNonDigit && !(content[previous] == ',' && std::isspace(content[firstNonDigit])) && !(content[previous] == 'x' && content[firstNonDigit] == '-'))
                        return TokenType::Invalid;  // It must have number(s) in between

                    if(std::isspace(content[firstNonDigit]) || content[firstNonDigit] == ',')
                    {
                        token.count = firstNonDigit - token.startPosition;
                        index = firstNonDigit;
                        auto v = token.ToView(content);
                        return token;
                    }

                    if(content[firstNonDigit] == 'x')
                    {
                        token.extra8++;
                        dotCount = 0;
                        continue;
                    }

                    if(content[firstNonDigit] == '.')
                    {
                        dotCount++;
                        if(dotCount > 1)
                            return TokenType::Invalid;  // Multi dimensional numbers can't contain more than 1 dot (for each number)

                        continue;
                    }
                }
            }





            /* Possible datetime formats
            *  2024-12-24T15:30:00       -> Date + Time without timezone info (Usually interpreted as local time)
            *  2024-12-24T15:30:00Z      -> Date + Time with timezone info (Z means utc/zulu time)
            *  2024-12-24T15:30:00+05:30 -> Date + Time with timezone info (5 hours and 30 minutes ahead of UTC)
            *  2024-12-24                -> Date
            *  15:30:00                  -> Time
            *  2024-12-24T15:30:00.123Z  -> Date + Time with timezone info (Z means utc/zulu time) and milliseconds (123ms)
            *  2024-W52-2                -> Year + Week + Weekday (52nd week of 2024, tuesday)
            *  2024-359                  -> Year + Day of Year (359th day of 2024)
            */

            /* Possible duration formats (if we wanna support it, currently we don't) (Note: not here, it starts with a letter)
            *  P3D              -> 3 days
            *  P2W              -> 2 weeks (14 days)
            *  P1Y2M3D          -> 1 year, 2 months, 3 days
            *  P2WT3H           -> 2 weeks and 3 hours
            *  P5DT4H30M        -> 5 days, 4 hours, and 30 minutes
            *  PT1H45M          -> 1 hour and 45 minutes
            *  P1Y2M3DT4H30M10S -> 1 year, 2 months, 3 days, 4 hours, 30 minutes, and 10 seconds
            *  P10M             -> 10 minutes
            *  PT10M            -> 10 minutes (alternative representation for time)
            *  PT1.5S           -> 1.5 seconds (1 second and 500 milliseconds)
            *  PT0.000001S      -> 1 microsecond (0.000001 seconds)
            *  P1.5Y            -> 1.5 years (1 year and 6 months)
            *  P3DT5H30M        -> 3 days, 5 hours, and 30 minutes
            *  PT0.5H           -> 30 minutes (0.5 hours)
            *  P2Y3M4DT5H6M7S   -> 2 years, 3 months, 4 days, 5 hours, 6 minutes, and 7 seconds
            */


            if(content[firstNonDigit] == '-')  // date or datetime
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                size_t firstNonDate = content.find_first_not_of("0123456789TZW-+:.", index);
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = content.size() - token.startPosition;
                    index = -1;
                    return token;
                }

                if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = firstNonDate - token.startPosition;
                    index = firstNonDate;
                    return token;
                }

                return TokenType::Invalid;  // Invalid character after timestamp
            }

            if(content[firstNonDigit] == ':')  // time
            {
                Token token = Token(TokenType::TimestampLiteral, index);
                token.line = line;
                token.column = token.startPosition - lastNewLineIndex;
                size_t firstNonDate = content.find_first_not_of("0123456789+:.", index);  // idk if it can include timezone ("+" sign)
                if(firstNonDate == std::string_view::npos)
                {
                    token.count = content.size() - token.startPosition;
                    index = -1;
                    return token;
                }

                if(std::isspace(content[firstNonDate]) || content[firstNonDate] == ',')
                {
                    token.count = firstNonDate - token.startPosition;
                    index = firstNonDate;
                    return token;
                }

                return TokenType::Invalid;  // Invalid character after timestamp
            }

            return TokenType::Invalid;  // Something we didn't process yet?
        }

        return TokenType::Invalid;  // Something we didn't process yet?
    }




    template<auto ERROR_CALLBACK>
    struct Utils
    {
        constexpr static bool ParseFileContent(std::string_view content, std::vector<Entry>& entries, std::string& fileComment) noexcept
        {
            Tokenizer tokenizer = content;
            Token fileCommentToken = TokenType::NonExisting;
            while(true)
            {
                Token comment = TokenType::NonExisting;
                Token type = TokenType::NonExisting;
                Token currentToken = tokenizer.Current();
                if(currentToken.type == TokenType::Invalid)
                    return false;
    
                while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
                {
                    if(currentToken.type == TokenType::Comment)
                    {
                        if(entries.empty() && fileComment.empty() && currentToken.count > 0 && content[currentToken.startPosition] == '#')
                        {
                            std::string_view sv = currentToken.ToView(content);
                            size_t firstChar = sv.find_first_not_of("# ");
                            if(firstChar == std::string_view::npos)
                            {
                                currentToken.startPosition += currentToken.count;
                                currentToken.count = 0;
                            }
                            else
                            {
                                currentToken.startPosition += firstChar;
                                currentToken.count = currentToken.count - firstChar;
                                if(content[currentToken.startPosition] == '\n')
                                {
                                    currentToken.startPosition++;
                                    currentToken.count--;
                                }
                            }

                            if(fileCommentToken.type != TokenType::NonExisting)
                                if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("File already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", fileCommentToken.ToView(content), fileCommentToken.line, fileCommentToken.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                    return false;
                            fileCommentToken = currentToken;
                        }
                        else
                        {
                            if(comment.type != TokenType::NonExisting)
                                if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                    return false;
                            comment = currentToken;
                        }
                    }
    
                    currentToken = tokenizer.Advance();
                }
    
                if(currentToken.type == TokenType::Identifier)
                {
                    if(!ParseVariable(content, tokenizer, entries, comment, -1))
                        return false;
    
                    continue;
                }
                
                if(currentToken.type == TokenType::EndOfFile)
                    break;
    
                return false;  // First token can't be anything else
            }

            if(fileCommentToken.type != TokenType::NonExisting)
                fileComment = fileCommentToken.ToView(content);
            return true;
        }
    
    
    
    
        constexpr static bool ParseVariable(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, Token comment, size_t parentEntryIndex)
        {
            const bool bHasParent    = parentEntryIndex != -1;
            const bool bArrayElement = bHasParent? entries[parentEntryIndex].type == Type::Array : false;
            Token currentToken = tokenizer.Current();
    
            Entry& entry = entries.emplace_back();
            size_t currentEntryIndex = entries.size() - 1;
    
            if(bArrayElement)
            {
                std::string temp = std::format("{}", entries[parentEntryIndex].size);
                entry.identifierSize = temp.size();
                entry.fullIdentifier = std::format("{}.{}", entries[parentEntryIndex].fullIdentifier, temp);
            }
            else
            {
                entry.fullIdentifier = currentToken.ToView(content);
                entry.identifierSize = entry.fullIdentifier.size();
                currentToken = tokenizer.Advance();
            }
    
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
    
            if(bHasParent)
            {
                if(parentEntryIndex >= entries.size())
                    return false;  // Unexpected parsing error
    
                Entry& parent = entries[parentEntryIndex];
                if(parent.type != Type::Array && parent.type != Type::Map)
                    return false;  // Unexpected parsing error
    
                entry.depth = parent.depth + 1;
                parent.size++;
                if(!bArrayElement)
                    entry.fullIdentifier = parent.fullIdentifier + '.' + entry.fullIdentifier;
            }
    
            
    
            bool bHasEqual = false;
            if(currentToken.type == TokenType::Equal)
            {
                bHasEqual = true;
                currentToken = tokenizer.Advance();
                CHECK_TOKEN(currentToken);
                CHECK_TOKEN_FOR_EOF(currentToken);
            }


            // We need to peek next tokens to figure out what comment belongs to what
            if(bHasEqual)
            {
                Token lastToken = TokenType::NonExisting;
                while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
                {
                    if(currentToken.type == TokenType::Comment)
                    {
                        if(comment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                return false;
                        comment = currentToken;
                    }

                    lastToken = currentToken;
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                    CHECK_TOKEN_FOR_EOF(currentToken);
                }

                if(IsValueLiteral(currentToken.type))
                    return ParseSimpleValue(content, tokenizer, entry, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
                if(currentToken.type == TokenType::CurlyBraceOpen)
                    return ParseMap(content, tokenizer, entries, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
                if(currentToken.type == TokenType::SquareBraceOpen)
                    return ParseArray(content, tokenizer, entries, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
            }
            else
            {
                Token backupToken = currentToken;
                Tokenizer backup = tokenizer;
                std::vector<Token> comments;
                if(comment.type != TokenType::NonExisting)
                    comments.push_back(comment);

                while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
                {
                    if(currentToken.type == TokenType::Comment)
                        comments.push_back(currentToken);

                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                }

                auto warnComments = [&]() -> bool
                {
                    for(size_t i = 1; i < comments.size(); i++)
                    {
                        Token previous = comments[i - 1];
                        Token current  = comments[i];
                        if(comment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", previous.ToView(content), previous.line, previous.column, current.ToView(content), current.line, current.column)))
                                return false;
                    }

                    if(!comments.empty())
                        comment = comments[comments.size() - 1];
                    return true;
                };

                if(IsValueLiteral(currentToken.type))
                    return warnComments() && ParseSimpleValue(content, tokenizer, entry, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
                if(currentToken.type == TokenType::CurlyBraceOpen)
                    return warnComments() && ParseMap(content, tokenizer, entries, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
                if(currentToken.type == TokenType::SquareBraceOpen)
                    return warnComments() && ParseArray(content, tokenizer, entries, comment) && OverrideEntry(entries, FindEntry(entries, entries[currentEntryIndex].fullIdentifier, entries[currentEntryIndex].depth, bHasParent? parentEntryIndex + 1 : 0), currentEntryIndex);
            }
    
            return false;  // Something we didn't process yet?
        }
    
    
    
    
        constexpr static bool ParseSimpleValue(std::string_view content, Tokenizer& tokenizer, Entry& entry, Token comment)
        {
            Token currentToken = tokenizer.Current();
            std::string_view view = currentToken.ToView(content);
    
            auto postProcess = [&]()
            {
                currentToken = tokenizer.Advance();
                if(currentToken.type == TokenType::Comment)
                {
                    if(comment.type != TokenType::NonExisting)
                        if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                            return false;
                    comment = currentToken;
                    currentToken = tokenizer.Advance();
                }
    
                if(currentToken.type == TokenType::NewLine)
                    tokenizer.Advance();

                if(comment.type != TokenType::NonExisting)
                    entry.comment = comment.ToView(content);
                return true;
            };
            
            if(currentToken.type == TokenType::Keyword)
            {
                if(currentToken.extra8 == 0)
                {
                    entry.type = Type::Null;
                    return postProcess();
                }
                if(currentToken.extra8 == 1 || currentToken.extra8 == 2)
                {
                    entry.type = Type::Bool;
                    entry.size = 1;
                    entry.data.b[0] = currentToken.extra8 == 1;
                    return postProcess();
                }
    
                return false;  // Invalid keyword when expected a value
            }
    
            entry.size = currentToken.extra8;  // Dimension count or string length
    
    
    
    
            if(currentToken.type == TokenType::IntLiteral)
            {
                bool bIsUnsigned = false;
                bool bContainsAnyNegative = false;
                bool bIsFirstChar = true;
                bool bIsNegative = false;
    
                uint64_t result = 0;
                uint8_t currentDimension = 0;
                const uint8_t dimensionCount = entry.size;
    
                auto finishDimension = [&]() -> bool
                {
                    if(bIsNegative)
                    {
                        if(bIsUnsigned || result > INT64_MAX_VALUE)
                            return false;
    
                        entry.data.i[currentDimension] = -result;
                    }
                    else
                    {
                        bool bWasUnsigned = bIsUnsigned;
                        if(result > INT64_MAX_VALUE)
                            bIsUnsigned = true;
    
                        if(bIsUnsigned)
                        {
                            if(bContainsAnyNegative)
                                return false;
    
                            if(!bWasUnsigned)
                            {
                                int64_t temp[VARIANT_64BIT_ELEMENT_COUNT];
                                for(uint8_t i = 0; i < currentDimension - 1; i++)
                                    temp[i] = entry.data.i[i];
    
                                for(uint8_t i = 0; i < currentDimension - 1; i++)
                                {
                                    if(temp[i] > INT64_MAX_VALUE)
                                        return false;
    
                                    entry.data.u[i] = temp[i];
                                }
                            }
    
                            entry.data.u[currentDimension] = result;
                        }
                        else
                            entry.data.i[currentDimension] = result;
                    }
    
                    return true;
                };
    
    
                for(size_t i = 0; i < view.size(); i++)
                {
                    char c = view[i];
                    if(bIsFirstChar && c == '-')
                    {
                        bIsNegative = true;
                        bContainsAnyNegative = true;
                    }
                    else if(std::isdigit(c))
                    {
                        if(result > UINT64_MAX_VALUE / 10)
                            return false;  // Overflow
    
                        result *= 10;
    
                        const uint64_t digit = c - '0';
                        if(result > UINT64_MAX_VALUE - digit)
                            return false; // Overflow
    
                        result += digit;
                    }
                    else if(c == '.')
                    {
                        while(i < view.size() && (view[i] == '.' || std::isdigit(view[i])))
                            i++;
                    }
                    else if(c == 'x')
                    {
                        if(currentDimension >= dimensionCount - 1)
                            return false;  // Too much dimensions
    
                        if(!finishDimension())
                            return false;
    
                        bIsFirstChar = true;
                        bIsNegative = false;
    
                        result = 0;
                        currentDimension++;
    
                        continue;
                    }
                    else
                        return false;  // unknown character
    
                    bIsFirstChar = false;
                }
    
                if(!finishDimension())
                    return false;
    
                entry.type = bIsUnsigned? Type::UInt : Type::Int;
                return postProcess();
            }
    
    
    
    
            if(currentToken.type == TokenType::FloatLiteral)
            {
                entry.type = Type::Float;
    
                bool bIsFirstChar = true;
                bool bIsNegative = false;
                bool bAfterDot = false;
    
                double multiplier = 1.0;
                double result = 0.0;
                uint8_t currentDimension = 0;
                const uint8_t dimensionCount = entry.size;
    
                for(size_t i = 0; i < view.size(); i++)
                {
                    char c = view[i];
                    if(bIsFirstChar && c == '-')
                    {
                        bIsNegative = true;
                    }
                    else if(std::isdigit(c))
                    {
                        if(result > DOUBLE_MAX_VALUE / 10)
                            return false;  // Overflow
    
                        if(!bAfterDot)
                            result *= 10;
    
                        const double value = static_cast<double>(c - '0') * multiplier;
                        if(result > DOUBLE_MAX_VALUE - value)
                            return false; // Overflow
    
                        result += value;
                    }
                    else if(c == '.')
                    {
                        if(bAfterDot)
                            return false;  // Can't contain multiple dots
    
                        bAfterDot = true;
                    }
                    else if(c == 'x')
                    {
                        if(currentDimension >= dimensionCount - 1)
                            return false;  // Too much dimensions
    
                        entry.data.f[currentDimension] = bIsNegative? -result : result;
    
                        bIsFirstChar = true;
                        bIsNegative = false;
                        bAfterDot = false;
    
                        multiplier = 1.0;
                        result = 0.0;
                        currentDimension++;
    
                        continue;
                    }
                    else
                        return false;  // unknown character
    
                    bIsFirstChar = false;
                    if(bAfterDot)
                        multiplier *= 0.1;
                }
    
                entry.data.f[currentDimension] = bIsNegative? -result : result;
                return postProcess();
            }
    
    
    
    
            if(currentToken.type == TokenType::VersionLiteral)
            {
                entry.type = Type::Version;
                entry.data.u[3] = 0;
    
                uint8_t currentDimension = 0;
                const uint8_t dimensionCount = entry.size;
    
                uint64_t result = 0;
                for(size_t i = 0; i < view.size(); i++)
                {
                    char c = view[i];
                    if(std::isdigit(c))
                    {
                        if(result > UINT64_MAX_VALUE / 10)
                            return false;  // Overflow
    
                        result *= 10;
    
                        const uint64_t digit = c - '0';
                        if(result > UINT64_MAX_VALUE - digit)
                            return false; // Overflow
    
                        result += digit;
                    }
                    else if(c == '.')
                    {
                        if(currentDimension >= dimensionCount - 1)
                            return false;  // Too much dimensions
    
                        entry.data.u[currentDimension] = result;
    
                        result = 0;
                        currentDimension++;
                    }
                    else
                        return false;  // unknown character
                }
    
                entry.data.u[currentDimension] = result;
                return postProcess();
            }
    
    
    
            
            size_t size = 0;
            bool bDynamic = false;
            auto writeCharacter = [&](char c)
            {
                if(size < VARIANT_SIZE)
                {
                    entry.data.str[size++] = c;
                }
                else
                {
                    if(!bDynamic)
                    {
                        bDynamic = true;
                        char buffer[VARIANT_SIZE] = {};
                        constexpr_memcpy(buffer, entry.data.str, size);
                        entry.data.strDynamic.InitialAllocate(entry.size + 1);
                        constexpr_memcpy(entry.data.strDynamic.data, buffer, size);
                    }
    
                    entry.data.strDynamic[size++] = c;
                }
            };
            auto writeDynamicCharacter = [&](char c)
            {
                entry.data.strDynamic[size++] = c;
            };
    
            if(currentToken.type == TokenType::StringLiteral)
            {
                entry.size = view.size() - 2;
                entry.type = Type::String;
    
                size_t escapeCharacters = 0;
                if(view.size() < VARIANT_DYNAMIC_STRING_HARD_LIMIT)
                {
                    for(int i = 1; i <= entry.size; i++)
                    {
                        if(view[i + escapeCharacters] == '\\')
                        {
                            entry.size--;
                            escapeCharacters++;
                        }
    
                        writeCharacter(view[i + escapeCharacters]);
                    }
                    writeCharacter('\0');
                }
                else
                {
                    entry.data.strDynamic.InitialAllocate(entry.size + 1);
                    for(int i = 1; i <= entry.size; i++)
                    {
                        if(view[i + escapeCharacters] == '\\')
                        {
                            entry.size--;
                            escapeCharacters++;
                        }
    
                        writeDynamicCharacter(view[i + escapeCharacters]);
                    }
    
                    writeDynamicCharacter('\0');
                    entry.data.strDynamic.RefreshView();
                }

                return postProcess();
            }
    
    
    
    
            if(currentToken.type == TokenType::HexLiteral || currentToken.type == TokenType::TimestampLiteral)
            {
                entry.size = view.size();
                entry.type = currentToken.type == TokenType::HexLiteral? Type::Hex : Type::Timestamp;
    
                if(view.size() + 1 < VARIANT_SIZE)
                {
                    constexpr_memcpy(entry.data.str, view.data(), view.size());
                    entry.data.str[view.size()] = '\0';
                }
                else
                {
                    entry.data.strDynamic.InitialAllocate(entry.size + 1);
                    constexpr_memcpy(entry.data.strDynamic.data, view.data(), view.size());
                    entry.data.strDynamic.data[view.size()] = '\0';
                    entry.data.strDynamic.RefreshView();
                }

                return postProcess();
            }
    
    
    
    
            if(currentToken.type == TokenType::EvaluateLiteral)
            {
                entry.size = EVALUATE_LITERAL_TEXT.size();
                entry.type = Type::String;
                constexpr_memcpy(entry.data.str, EVALUATE_LITERAL_TEXT.data(), EVALUATE_LITERAL_TEXT.size());

                return postProcess();
            }
    
            return false;  // Something we didn't process yet?
        }
    
    
    
    
        constexpr static bool ParseArray(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, Token comment)
        {
            size_t entryID = entries.size() - 1;
            entries[entryID].type = Type::Array;
    
            Token currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);
    
            while(true)
            {
                Token childComment;
                while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
                {
                    if(currentToken.type == TokenType::Comment)
                    {
                        if(childComment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                return false;
                        childComment = currentToken;
                    }
    
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                    CHECK_TOKEN_FOR_EOF(currentToken);
                }
    
    
                if(IsValueLiteral(currentToken.type) || currentToken.type == TokenType::CurlyBraceOpen || currentToken.type == TokenType::SquareBraceOpen)
                {
                    if(!ParseVariable(content, tokenizer, entries, childComment, entryID))
                        return false;
    
                    currentToken = tokenizer.Current();
                    if(currentToken.type == TokenType::Comma)
                    {
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                        CHECK_TOKEN_FOR_EOF(currentToken);
                    }
                }
                else if(currentToken.type == TokenType::SquareBraceClose)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);

                    if(currentToken.type == TokenType::Comment)
                    {
                        if(comment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                return false;
                        comment = currentToken;
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                    }
    
                    if(currentToken.type == TokenType::NewLine)
                    {
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                    }

                    if(comment.type != TokenType::NonExisting)
                        entries[entryID].comment = comment.ToView(content);
                    return true;
                }
                else
                    return false;
            }
        }
    
    
    
    
        constexpr static bool ParseMap(std::string_view content, Tokenizer& tokenizer, std::vector<Entry>& entries, Token comment)
        {
            size_t entryID = entries.size() - 1;
            entries[entryID].type = Type::Map;
    
            //if(entries[entryID].typeID == 1)
            //    CopyEntryDeep(entries, userTypes, userTypeID);
    
            Token currentToken = tokenizer.Advance();
            CHECK_TOKEN(currentToken);
            CHECK_TOKEN_FOR_EOF(currentToken);

            while(true)
            {
                Token childComment;
                while(currentToken.type == TokenType::Comment || currentToken.type == TokenType::NewLine)
                {
                    if(currentToken.type == TokenType::Comment)
                    {
                        if(childComment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", childComment.ToView(content), childComment.line, childComment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                return false;
                        childComment = currentToken;
                    }
    
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);
                    CHECK_TOKEN_FOR_EOF(currentToken);
                }
    
    
                if(currentToken.type == TokenType::Identifier)
                {
                    if(!ParseVariable(content, tokenizer, entries, childComment, entryID))
                        return false;
    
                    currentToken = tokenizer.Current();
                    if(currentToken.type == TokenType::Comma)
                    {
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                        CHECK_TOKEN_FOR_EOF(currentToken);
                    }
                }
                else if(currentToken.type == TokenType::CurlyBraceClose)
                {
                    currentToken = tokenizer.Advance();
                    CHECK_TOKEN(currentToken);

                    if(currentToken.type == TokenType::Comment)
                    {
                        if(comment.type != TokenType::NonExisting)
                            if(!ERROR_CALLBACK(Error::AlreadyHasComment, std::format("Token already has a comment\nOld Comment: \"{}\" ({}:{})\nNew Comment: \"{}\" ({}:{})", comment.ToView(content), comment.line, comment.column, currentToken.ToView(content), currentToken.line, currentToken.column)))
                                return false;
                        comment = currentToken;
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                    }
    
                    if(currentToken.type == TokenType::NewLine)
                    {
                        currentToken = tokenizer.Advance();
                        CHECK_TOKEN(currentToken);
                    }

                    if(comment.type != TokenType::NonExisting)
                        entries[entryID].comment = comment.ToView(content);
                    return true;
                }
                else
                    return false;
            }
        }
    
    
    
    
        constexpr static size_t FindEntry(const std::vector<Entry>& entries, std::string_view fullIdentifier, size_t depth, size_t startIndex)
        {
            for(size_t i = startIndex; i < entries.size(); i++)
            {
                if(entries[i].depth == depth && entries[i].fullIdentifier == fullIdentifier)
                    return i;
            }
    
            return -1;
        }
    
        constexpr static void CopyEntryDeep(std::vector<Entry>& target, const std::vector<Entry>& source, size_t sourceID)
        {
            size_t targetID = target.size() - 1;
    
            target[targetID].type = source[sourceID].type;
            target[targetID].size = source[sourceID].size;
    
            for(size_t i = 0; i < source[sourceID].size; i++)
            {
                const size_t index = sourceID + i + 1;
                Entry& last = target.emplace_back(source[index]);
                last.depth += target[targetID].depth;
                last.fullIdentifier = std::format("{}{}", target[targetID].fullIdentifier, source[index].GetIdentifierWithDot());
                if(last.type == Type::Array || last.type == Type::Map)
                    CopyEntryDeep(target, source, index);
            }
        }
    
        constexpr static bool OverrideEntry(std::vector<Entry>& entries, size_t targetID, size_t sourceID)
        {
            // TODO: implement or completely get rid of overriding...
            // For now, we don't allow any kind of name collision (we don't override anything)
            return targetID == sourceID;


            /*
            if(targetID == sourceID)
                return true;
    
            if(entries[targetID].type == entries[sourceID].type)
            {
                if(entries[targetID].type != Type::Array && entries[targetID].type != Type::Map)
                {
                    entries[targetID] = entries[sourceID];
                    entries.pop_back();
                    return true;
                }
                else
                {
                    // TODO: handle Array and Map
                    // TODO: Just override every child and their childs and so on... Don't bother with merging or something like that
                    return false;
                }
            }
    
            return false;  // Something we didn't process yet?
            */
        }










        template<Style STYLE>
        constexpr static bool WriteFileContent(std::string& buffer, std::vector<Entry>& entries, std::string& fileComment)
        {
            return false;  // TODO: implement
        }
    };
}













namespace fdf
{
    template<auto ERROR_CALLBACK> requires(detail::IsValidErrorCallback<decltype(ERROR_CALLBACK)>)
    class IO
    {
        friend struct detail::Test;

    public:
        constexpr IO() noexcept = default;

    public:
        constexpr bool Parse(std::string_view content, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept
        {
            IO other;
            if(!detail::Utils<ERROR_CALLBACK>::ParseFileContent(content, other.entries, other.fileComment))
                return false;

            return Combine(other, fileCommentCombineStrategy);
        }
        inline bool Parse(std::filesystem::path filepath, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept
        {
            if(!std::filesystem::exists(filepath) || !std::filesystem::is_regular_file(filepath))
                return false;

            std::ifstream file(filepath);
            if(!file)
                return false;

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            IO other;
            if(!detail::Utils<ERROR_CALLBACK>::ParseFileContent(content, other.entries, other.fileComment))
                return false;

            return Combine(other, fileCommentCombineStrategy);
        }
        template<auto OTHER_ERROR_CALLBACK>
        constexpr bool Combine(const IO<OTHER_ERROR_CALLBACK>& other, CommentCombineStrategy fileCommentCombineStrategy = CommentCombineStrategy::UseExisting) noexcept
        {
            switch(fileCommentCombineStrategy)
            {
                case CommentCombineStrategy::UseExisting: break;
                case CommentCombineStrategy::UseNew: fileComment = other.fileComment; break;
                case CommentCombineStrategy::UseNewIfExistingIsEmpty: 
                    if(fileComment.empty())
                        fileComment = other.fileComment;
                    break;
                case CommentCombineStrategy::Merge:
                    if(fileComment.empty())
                        fileComment = other.fileComment;
                    else if(!other.fileComment.empty())
                        fileComment = fileComment + '\n' + other.fileComment;
                    break;
                case CommentCombineStrategy::Clear: fileComment.clear(); break;
                default: std::unreachable();
            }

            entries.insert(entries.end(), other.entries.begin(), other.entries.end());
            return true;
        }

    public:
        template<Style STYLE = {}>
        constexpr bool WriteToBuffer(std::string& buffer) noexcept
        {
            return detail::Utils<ERROR_CALLBACK>::WriteFileContent(buffer, entries, fileComment);
        }
        template<Style STYLE = {}>
        inline bool WriteToFile(std::filesystem::path filepath, bool bCreateIfNotExists = true) noexcept
        {
            if(!std::filesystem::exists(filepath))
            {
                if(!bCreateIfNotExists)
                    return false;

                auto parentDir = filepath.parent_path();
                if(!parentDir.empty() && !std::filesystem::exists(parentDir))
                {
                    std::error_code ec;
                    std::filesystem::create_directories(parentDir, ec);
                    if(ec)
                        return false;
                }
            }
            else if(!std::filesystem::is_regular_file(filepath))
            {
                return false;
            }

            std::string buffer;
            if(!WriteToBuffer(buffer))
                return false;

            std::ofstream file(filepath);
            if(!file)
                return false;

            file << buffer;
        }

    public:
        constexpr size_t GetEntryCount()         const noexcept  { return entries.size(); }
        constexpr size_t GetTopLevelEntryCount() const noexcept  { return topLevelEntryCount; }

        // Call const versions then cast away const-ness so we don't duplicate the code
        constexpr Entry& GetEntryMutable(size_t id) noexcept
        {
            return const_cast<Entry&>(static_cast<const IO*>(this)->GetEntry(id));
        }
        constexpr Entry& GetEntryMutable(std::string_view identifier) noexcept
        {
            return const_cast<Entry&>(static_cast<const IO*>(this)->GetEntry(identifier));
        }
        constexpr Entry& GetTopLevelEntryMutable(size_t id) noexcept
        {
            return const_cast<Entry&>(static_cast<const IO*>(this)->GetTopLevelEntry(id));
        }

        constexpr const Entry& GetEntry(size_t id) const noexcept
        {
            return entries.size() > id? entries[id] : Entry::INVALID;
        }
        constexpr const Entry& GetEntry(std::string_view identifier) const noexcept
        {
            const size_t id = detail::Utils<ERROR_CALLBACK>::FindEntry(entries, identifier, std::ranges::count(identifier, '.'), 0);
            return GetEntry(id);
        }
        constexpr const Entry& GetTopLevelEntry(size_t id) const noexcept
        {
            size_t currentTopLevelCount = 0;
            for(size_t i = 0; i < entries.size() && currentTopLevelCount < topLevelEntryCount; i++)
            {
                if(entries[i].depth == 0)
                {
                    if(id == currentTopLevelCount++)
                        return entries[i];
                }
            }

            return Entry::INVALID;
        }
        

        constexpr auto Iterator()               noexcept  { }  // TODO: implement
        constexpr auto Iterator()         const noexcept  { }  // TODO: implement
        constexpr auto TopLevelIterator()       noexcept  { }  // TODO: implement
        constexpr auto TopLevelIterator() const noexcept  { }  // TODO: implement

    private:
        std::vector<Entry> entries;
        size_t topLevelEntryCount = 0;  // TODO: implement

    public:
        std::string fileComment;
    };
}










#undef FDF_EXPORT
#undef CHECK_TOKEN
#undef CHECK_TOKEN_FOR_EOF
