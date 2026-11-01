// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include <array>
#include <atomic>
#include <bitset>
#include <cstdint>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

class StateArchive
{
public:
    explicit StateArchive(std::vector<uint8_t> &output) : m_output(&output) {}
    explicit StateArchive(const std::vector<uint8_t> &input) : m_input(&input) {}

    [[nodiscard]] bool loading() const noexcept { return m_input != nullptr; }
    [[nodiscard]] bool finished() const noexcept { return !m_input || m_position == m_input->size(); }

    void bytes(void *data, size_t size)
    {
        if (!size)
            return;
        if (m_input)
        {
            if (m_position + size > m_input->size())
                throw std::runtime_error("Save state is truncated");
            std::memcpy(data, m_input->data() + m_position, size);
            m_position += size;
            return;
        }
        const size_t at = m_output->size();
        m_output->resize(at + size);
        std::memcpy(m_output->data() + at, data, size);
    }

    void section(uint32_t tag)
    {
        uint32_t value = tag;
        bytes(&value, sizeof value);
        if (value != tag)
            throw std::runtime_error("Save state section mismatch");
    }

    template <typename T>
    void operator()(T &value)
    {
        archive(*this, value);
    }

    template <typename T, typename... Rest>
    void operator()(T &value, Rest &...rest)
    {
        archive(*this, value);
        (*this)(rest...);
    }

private:
    std::vector<uint8_t> *m_output = nullptr;
    const std::vector<uint8_t> *m_input = nullptr;
    size_t m_position = 0;
};

template <typename T>
inline constexpr bool state_plain_v = std::is_trivially_copyable_v<T> && !std::is_pointer_v<T>;

template <typename T>
    requires state_plain_v<T>
void archive(StateArchive &a, T &value)
{
    a.bytes(&value, sizeof value);
}

template <typename T>
    requires requires(T &value, StateArchive &a) { value.serializeState(a); }
void archive(StateArchive &a, T &value)
{
    value.serializeState(a);
}

template <typename T>
void archive(StateArchive &a, std::atomic<T> &value)
{
    T plain = value.load(std::memory_order_acquire);
    archive(a, plain);
    if (a.loading())
        value.store(plain, std::memory_order_release);
}

inline void archiveSize(StateArchive &a, uint64_t &size) { a.bytes(&size, sizeof size); }

inline void archive(StateArchive &a, std::string &value)
{
    uint64_t size = value.size();
    archiveSize(a, size);
    if (a.loading())
        value.resize(size_t(size));
    a.bytes(value.data(), value.size());
}

template <typename T, typename Alloc>
void archive(StateArchive &a, std::vector<T, Alloc> &value)
{
    uint64_t size = value.size();
    archiveSize(a, size);
    if constexpr (std::is_same_v<T, bool>)
    {
        if (a.loading())
            value.assign(size_t(size), false);
        for (size_t i = 0; i < value.size(); ++i)
        {
            bool bit = value[i];
            a.bytes(&bit, 1);
            value[i] = bit;
        }
    }
    else if constexpr (state_plain_v<T>)
    {
        if (a.loading())
            value.resize(size_t(size));
        a.bytes(value.data(), value.size() * sizeof(T));
    }
    else
    {
        if (a.loading())
            value.resize(size_t(size));
        for (auto &item : value)
            archive(a, item);
    }
}

template <typename T, size_t N>
void archive(StateArchive &a, std::array<T, N> &value)
{
    if constexpr (state_plain_v<T>)
        a.bytes(value.data(), sizeof(T) * N);
    else
        for (auto &item : value)
            archive(a, item);
}

template <typename T>
void archive(StateArchive &a, std::deque<T> &value)
{
    uint64_t size = value.size();
    archiveSize(a, size);
    if (a.loading())
        value.resize(size_t(size));
    for (auto &item : value)
        archive(a, item);
}

template <typename A, typename B>
void archive(StateArchive &a, std::pair<A, B> &value)
{
    archive(a, value.first);
    archive(a, value.second);
}

template <typename Map>
void archiveMap(StateArchive &a, Map &value)
{
    uint64_t size = value.size();
    archiveSize(a, size);
    if (a.loading())
    {
        value.clear();
        for (uint64_t i = 0; i < size; ++i)
        {
            typename Map::key_type key{};
            archive(a, key);
            typename Map::mapped_type mapped{};
            archive(a, mapped);
            value.emplace(std::move(key), std::move(mapped));
        }
        return;
    }
    for (auto &[key, mapped] : value)
    {
        auto copy = key;
        archive(a, copy);
        archive(a, mapped);
    }
}

template <typename K, typename V, typename C, typename Al>
void archive(StateArchive &a, std::map<K, V, C, Al> &value) { archiveMap(a, value); }
template <typename K, typename V, typename C, typename Al>
void archive(StateArchive &a, std::multimap<K, V, C, Al> &value) { archiveMap(a, value); }
template <typename K, typename V, typename H, typename E, typename Al>
void archive(StateArchive &a, std::unordered_map<K, V, H, E, Al> &value) { archiveMap(a, value); }

template <typename Set>
void archiveSet(StateArchive &a, Set &value)
{
    uint64_t size = value.size();
    archiveSize(a, size);
    if (a.loading())
    {
        value.clear();
        for (uint64_t i = 0; i < size; ++i)
        {
            typename Set::key_type key{};
            archive(a, key);
            value.insert(std::move(key));
        }
        return;
    }
    for (const auto &key : value)
    {
        auto copy = key;
        archive(a, copy);
    }
}

template <typename K, typename C, typename Al>
void archive(StateArchive &a, std::set<K, C, Al> &value) { archiveSet(a, value); }
template <typename K, typename H, typename E, typename Al>
void archive(StateArchive &a, std::unordered_set<K, H, E, Al> &value) { archiveSet(a, value); }

template <typename T>
void archive(StateArchive &a, std::optional<T> &value)
{
    bool present = value.has_value();
    a.bytes(&present, 1);
    if (a.loading())
    {
        if (present)
            value.emplace();
        else
            value.reset();
    }
    if (present)
        archive(a, *value);
}

template <size_t I, typename Variant>
void loadAlternative(StateArchive &a, Variant &value, uint64_t index)
{
    if constexpr (I < std::variant_size_v<Variant>)
    {
        if (index == I)
        {
            value.template emplace<I>();
            archive(a, std::get<I>(value));
            return;
        }
        loadAlternative<I + 1>(a, value, index);
    }
    else
        throw std::runtime_error("Save state holds an unknown variant");
}

template <typename... Types>
void archive(StateArchive &a, std::variant<Types...> &value)
{
    uint64_t index = value.index();
    archiveSize(a, index);
    if (a.loading())
    {
        loadAlternative<0>(a, value, index);
        return;
    }
    std::visit([&](auto &held) { archive(a, held); }, value);
}

inline void archive(StateArchive &, std::monostate &) {}
