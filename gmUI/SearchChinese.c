#include <windows.h>
#include <tchar.h>
#include <string.h>

typedef struct SpellCode
{
    unsigned short firstCode;
    unsigned short lastCode;
    WCHAR returnLetter;
} SpellCode;

static const SpellCode g_spellCodeTable[] = 
{
    { 0xB0A1, 0xB0C4, L'a' },
    { 0XB0C5, 0XB2C0, L'b' },
    { 0xB2C1, 0xB4ED, L'c' },
    { 0xB4EE, 0xB6E9, L'd' },
    { 0xB6EA, 0xB7A1, L'e' },
    { 0xB7A2, 0xB8C0, L'f' },
    { 0xB8C1, 0xB9FD, L'g' },
    { 0xB9FE, 0xBBF6, L'h' },
    { 0xBBF7, 0xBFA5, L'j' },
    { 0xBFA6, 0xC0AB, L'k' },
    { 0xC0AC, 0xC2E7, L'l' },
    { 0xC2E8, 0xC4C2, L'm' },
    { 0xC4C3, 0xC5B5, L'n' },
    { 0xC5B6, 0xC5BD, L'o' },
    { 0xC5BE, 0xC6D9, L'p' },
    { 0xC6DA, 0xC8BA, L'q' },
    { 0xC8BB, 0xC8F5, L'r' },
    { 0xC8F6, 0xCBF9, L's' },
    { 0xCBFA, 0xCDD9, L't' },
    { 0xCDDA, 0xCEF3, L'w' },
    { 0xCEF4, 0xD1B8, L'x' },
    { 0xD1B9, 0xD4D0, L'y' },
    { 0xD4D1, 0xD7F9, L'z' },
};
static const int g_tableCount = 23;

static const SpellCode g_spellCodeTable1[] = 
{
    { (unsigned short)('æä'), 0, L'y' },
    { (unsigned short)('æê'), 0, L'l' },
    { (unsigned short)('æ¦'), 0, L's' },
    { (unsigned short)('çÑ'), 0, L'm' },
    { (unsigned short)('éª'), 0, L'n' },
};
static const int g_tableCount1 = 5;


static WCHAR GetSpellCode(WCHAR wch)
{
    int i;
    unsigned short ch;
    UCHAR sz[3];

    WideCharToMultiByte(CP_ACP, 0, &wch, 1, (CHAR *)sz, 3, NULL, NULL);
    ch = sz[0];
    ch <<= 8;
    ch += sz[1];

    for (i=0; i<g_tableCount1; i++)
    {
        if (ch == g_spellCodeTable1[i].firstCode)
            return g_spellCodeTable1[i].returnLetter;
    }

    for (i=0; i<g_tableCount; i++)
    {
        if (ch >= g_spellCodeTable[i].firstCode &&
            ch <= g_spellCodeTable[i].lastCode)
            return g_spellCodeTable[i].returnLetter;
    }

    return wch;
}

WCHAR *GetSpellString(WCHAR *_in, WCHAR *_out, int _outLen)
{
    int i;

    for (i=0; i<(int)wcslen(_in) && i<_outLen-1; i++)
        _out[i] = GetSpellCode(_in[i]);
    _out[i] = 0;

    return _out;
}

