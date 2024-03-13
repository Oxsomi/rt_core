/* OxC3/RT Core(Oxsomi core 3/RT Core), a general framework for raytracing applications.
*  Copyright (C) 2023 Oxsomi / Nielsbishere (Niels Brunekreef)
*
*  This program is free software: you can redistribute it and/or modify
*  it under the terms of the GNU General Public License as published by
*  the Free Software Foundation, either version 3 of the License, or
*  (at your option) any later version.
*
*  This program is distributed in the hope that it will be useful,
*  but WITHOUT ANY WARRANTY; without even the implied warranty of
*  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*  GNU General Public License for more details.
*
*  You should have received a copy of the GNU General Public License
*  along with this program. If not, see https://github.com/Oxsomi/core3/blob/main/LICENSE.
*  Be aware that GPL3 requires closed source products to be GPL3 too if released to the public.
*  To prevent this a separate license will have to be requested at contact@osomi.net for a premium;
*  This is called dual licensing.
*/

#pragma once
#include "types/vec.h"

extern const F32 AtmosHelper_au;

F32x4 AtmosHelper_equatorialToCartesian(F32 azimuth, F32 altitude, F32 radius);

F64 AtmosHelper_getJulianDate(Ns time);
F64 AtmosHelper_getJulianCenturies2000(F64 julianDate);

F32 AtmosHelper_getSolarTime(F64 JD, F32 hoursGmt, F32 longitudeRad);

F32x4 AtmosHelper_getSunDir(F64 JD, F32x2 longitudeLatitudeDeg);
F32x4 AtmosHelper_getSunPos(F64 JD, F32x2 longitudeLatitudeDeg);
