#include "Settings/AINpcSettings.h"

UAINpcSettings::UAINpcSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("AINpc");
}

FName UAINpcSettings::GetCategoryName() const
{
	return CategoryName;
}

FName UAINpcSettings::GetSectionName() const
{
	return SectionName;
}
