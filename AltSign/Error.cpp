#include "Error.hpp"

std::string NSLocalizedDescriptionKey = "NSLocalizedDescription";
std::string NSLocalizedFailureErrorKey = "NSLocalizedFailure";
std::string NSLocalizedFailureReasonErrorKey = "NSLocalizedFailureReason";
std::string NSLocalizedRecoverySuggestionErrorKey = "NSLocalizedRecoverySuggestion";
std::string NSUnderlyingErrorKey = "NSUnderlyingError";

std::string AnyStringValue(std::any& any)
{
    try
    {
        try
        {
            std::string string = std::any_cast<std::string>(any);
            return string;
        }
        catch (std::bad_any_cast)
        {
            const char* string = std::any_cast<const char*>(any);
            return string;
        }
    }
    catch (std::bad_any_cast)
    {
        int number = std::any_cast<int>(any);
        return std::to_string(number);
    }
}