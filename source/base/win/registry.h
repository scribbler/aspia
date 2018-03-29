//
// PROJECT:         Aspia
// FILE:            base/win/registry.h
// LICENSE:         GNU Lesser General Public License 2.1
// PROGRAMMERS:     Dmitry Chapyshev (dmitry@aspia.ru)
//

#ifndef _ASPIA_BASE__WIN__REGISTRY_H
#define _ASPIA_BASE__WIN__REGISTRY_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <vector>

namespace aspia {

class RegistryKey
{
public:
    RegistryKey() = default;
    explicit RegistryKey(HKEY key);
    RegistryKey(HKEY rootkey, const wchar_t* subkey, REGSAM access);

    ~RegistryKey();

    // True while the key is valid.
    bool IsValid() const;

    LONG Create(HKEY rootkey, const wchar_t* subkey, REGSAM access);

    LONG CreateWithDisposition(HKEY rootkey, const wchar_t* subkey,
                               DWORD *disposition, REGSAM access);

    // Opens an existing reg key.
    LONG Open(HKEY rootkey, const wchar_t* subkey, REGSAM access);

    //
    // Returns false if this key does not have the specified value, or if an error
    // occurrs while attempting to access it.
    //
    bool HasValue(const wchar_t* name) const;

    //
    // Reads raw data into |data|. If |name| is null or empty, reads the key's
    // default value, if any.
    //
    LONG ReadValue(const wchar_t* name, void* data, DWORD* dsize, DWORD* dtype) const;

    //
    // Reads a REG_DWORD (uint32_t) into |out_value|. If |name| is null or empty,
    // reads the key's default value, if any.
    //
    LONG ReadValueDW(const wchar_t* name, DWORD* out_value) const;

    //
    // Reads a REG_BINARY (array of chars) into |out_value|. If |name| is null or empty,
    // reads the key's default value, if any.
    //
    LONG ReadValueBIN(const wchar_t* name, std::string* out_value) const;

    //
    // Reads a string into |out_value|. If |name| is null or empty, reads
    // the key's default value, if any.
    //
    LONG ReadValue(const wchar_t* name, std::wstring* out_value) const;

    // Sets raw data, including type.
    LONG WriteValue(const wchar_t* name, const void* data, DWORD dsize, DWORD dtype);

    // Sets an int32_t value.
    LONG WriteValue(const wchar_t* name, DWORD in_value);

    // Sets a string value.
    LONG WriteValue(const wchar_t* name, const wchar_t* in_value);

    // Closes this reg key.
    void Close();

private:
    HKEY key_ = nullptr;
    REGSAM wow64access_ = 0;

    Q_DISABLE_COPY(RegistryKey)
};

// Iterates the entries found in a particular folder on the registry.
class RegistryValueIterator
{
public:
    // Constructs a Registry Value Iterator with default WOW64 access.
    RegistryValueIterator(HKEY root_key, const wchar_t* folder_key);

    // Constructs a Registry Key Iterator with specific WOW64 access, one of
    // KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0.
    // Note: |wow64access| should be the same access used to open |root_key|
    // previously, or a predefined key (e.g. HKEY_LOCAL_MACHINE).
    // See http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
    RegistryValueIterator(HKEY root_key,
                          const wchar_t* folder_key,
                          REGSAM wow64access);

    ~RegistryValueIterator();

    DWORD ValueCount() const;

    // True while the iterator is valid.
    bool Valid() const;

    // Advances to the next registry entry.
    void operator++();

    const wchar_t* Name() const { return name_.c_str(); }
    const wchar_t* Value() const { return value_.data(); }
    // ValueSize() is in bytes.
    DWORD ValueSize() const { return value_size_; }
    DWORD Type() const { return type_; }

    int Index() const { return index_; }

private:
    // Reads in the current values.
    bool Read();

    void Initialize(HKEY root_key, const wchar_t* folder_key, REGSAM wow64access);

    // The registry key being iterated.
    HKEY key_ = nullptr;

    // Current index of the iteration.
    int index_;

    // Current values.
    std::wstring name_;
    std::vector<wchar_t> value_;
    DWORD value_size_;
    DWORD type_;

    Q_DISABLE_COPY(RegistryValueIterator)
};

class RegistryKeyIterator
{
public:
    // Constructs a Registry Key Iterator with default WOW64 access.
    RegistryKeyIterator(HKEY root_key, const wchar_t* folder_key);

    // Constructs a Registry Value Iterator with specific WOW64 access, one of
    // KEY_WOW64_32KEY or KEY_WOW64_64KEY, or 0.
    // Note: |wow64access| should be the same access used to open |root_key|
    // previously, or a predefined key (e.g. HKEY_LOCAL_MACHINE).
    // See http://msdn.microsoft.com/en-us/library/windows/desktop/aa384129.aspx.
    RegistryKeyIterator(HKEY root_key, const wchar_t* folder_key, REGSAM wow64access);

    ~RegistryKeyIterator();

    DWORD SubkeyCount() const;

    // True while the iterator is valid.
    bool Valid() const;

    // Advances to the next entry in the folder.
    void operator++();

    const wchar_t* Name() const { return name_; }

    int Index() const { return index_; }

private:
    // Reads in the current values.
    bool Read();

    void Initialize(HKEY root_key, const wchar_t* folder_key, REGSAM wow64access);

    // The registry key being iterated.
    HKEY key_ = nullptr;

    // Current index of the iteration.
    int index_;

    wchar_t name_[MAX_PATH];

    Q_DISABLE_COPY(RegistryKeyIterator)
};

} // namespace aspia

#endif // _ASPIA_BASE__WIN__REGISTRY_H