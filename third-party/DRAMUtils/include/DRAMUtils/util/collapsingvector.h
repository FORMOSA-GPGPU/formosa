#ifndef DRAMUTILS_UTIL_COLLAPSINGVECTOR
#define DRAMUTILS_UTIL_COLLAPSINGVECTOR

#include "json.h"

#include <vector>

namespace DRAMUtils::util
{

template <typename T, typename Alloc = std::allocator<T>>
class CollapsingVector : public std::vector<T, Alloc>
{
public:
    enum class Type
    {
        SINGLE,
        ARRAY
    };

    using std::vector<T>::vector;

    // Forward constructor
    // Single element represented as the element type in serialization
    template <typename... Ts, typename = decltype(std::vector<T>{std::declval<Ts>()...})>
    CollapsingVector(Ts&&... ts) : std::vector<T>{std::forward<Ts>(ts)...}
    {
        if (this->size() == 1)
        {
            set_type(Type::SINGLE);
        }
        else
        {
            set_type(Type::ARRAY);
        }
    }

    // Forward constructor with type
    // This constructor allows a single element to be represented as an array in serialization
    template <typename... Ts, typename = decltype(std::vector<T>{std::declval<Ts>()...})>
    explicit CollapsingVector(Type type, Ts&&... ts) : std::vector<T>{std::forward<Ts>(ts)...}
    {
        // Overwrite type for multiple elements
        if (this->size() > 1)
        {
            set_type(Type::ARRAY);
        }
        else
        {
            set_type(type);
        }
    }

private:
    void set_type(Type type) { this->type = type; }

public:
    Type get_type() const { return type; }

    void from_json(const json_t& j)
    {
        this->clear();
        if (!j.is_array())
        {
            // Single element
            this->emplace_back(j.get<T>());
            set_type(Type::SINGLE);
        }
        else
        {
            // Array
            for (const auto& element : j)
            {
                this->emplace_back(element.get<T>());
            }
            set_type(Type::ARRAY);
        }
    }

    void to_json(json_t& j) const
    {
        if (get_type() == Type::SINGLE && this->size() == 1)
        {
            // Single element
            j = this->operator[](0);
        }
        else
        {
            // Array
            j = json_t::array();
            for (const auto& element : *this)
            {
                j.emplace_back(element);
            }
        }
    }

private:
    Type type = Type::ARRAY;
};

} // namespace DRAMUtils::util

#endif /* DRAMUTILS_UTIL_COLLAPSINGVECTOR */
