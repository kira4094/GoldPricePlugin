#include <windows.h>
#include "fixtures.h"

std::string GoldPriceTestHttpGet(bool london)
{
    Sleep(london ? 1200 : 800);
    return london ? LondonFixture : ZheShangFixture;
}
