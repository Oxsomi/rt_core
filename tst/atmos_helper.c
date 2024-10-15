
/* OxC3/RT Core(Oxsomi core 3/RT Core), a general framework for raytracing applications.
*  Copyright (C) 2023 - 2024 Oxsomi / Nielsbishere (Niels Brunekreef)
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

#include "atmos_helper.h"
#include "types/base/time.h"
#include "types/math/math.h"

const F32 AtmosHelper_au = 1.496e11f;

F32x4 AtmosHelper_equatorialToCartesian(F32 azimuth, F32 altitude, F32 radius) {

	const F32 cosAltitude = F32_cos(altitude);

	return F32x4_mul(F32x4_create3(
		cosAltitude * F32_sin(azimuth),
		cosAltitude * F32_cos(azimuth),
		F32_sin(altitude)
	), F32x4_xxxx4(radius));
}

F64 AtmosHelper_getJulianDate(Ns time) {

	U16 year;
	U8 month, day, hour, minute, second;
	U32 ns;

	if(!Time_getDate(time, &year, &month, &day, &hour, &minute, &second, &ns, false))
		return -1;

	const F64 seconds = 65 + second + ns / (F64)SECOND;
	const F64 minutes = minute + seconds / 60;
	const F64 hours = hour + minutes / 60;
	const F64 days = day + hours / 24;

	const F64 monthAdj = month <= 2 ? month + 12 : month;
	const F64 yearAdj = month <= 2 ? year - 1 : year;

	F64 julianDate = days + F64_floor(30.6001 * (monthAdj + 1));
	julianDate += F64_floor(365.25 * yearAdj);
	julianDate += F64_floor(yearAdj / 400) - F64_floor(yearAdj / 100);
	julianDate += 1720996.5;
	return julianDate;
}

F64 AtmosHelper_getJulianCenturies2000(F64 julianDate) {
	return (julianDate - 2451545.0) / 36525;
}

F32 AtmosHelper_getSolarTime(F64 JD, F32 hoursGmt, F32 longitudeRad) {
	return
		hoursGmt +
		0.17f * (F32) F64_sin(4 * F64_PI * (JD - 80) / 373) +
		-0.129f * (F32) F64_sin(2 * F64_PI * (JD - 8) / 355) +
		12 * -longitudeRad / F32_PI;
}

F32x4 AtmosHelper_getSunPosInternal(F64 JD, F32x2 longitudeLatitudeDeg, F32 rad) {

	const F32x2 longitudeLatitudeRad = F32x2_mul(longitudeLatitudeDeg, F32x2_xx2(F32_DEG_TO_RAD));

	const F32 longitudeRad = F32x2_x(longitudeLatitudeRad);
	const F32 latitudeRad = F32x2_y(longitudeLatitudeRad);

	const F32 solarTime = AtmosHelper_getSolarTime(JD, (F32) F64_fract(JD - .5) * 24, longitudeRad);
	const F32 solarTimePi12th = F32_PI * solarTime / 12;

	const F32 declination = 0.4093f * (F32) F64_sin(2 * F64_PI * (JD - 81) / 368);
	const F32 azimuth =
		F32_PI / 2 - F32_asin(
			F32_sin(latitudeRad) * F32_sin(declination) -
			F32_cos(latitudeRad) * F32_cos(declination) * F32_cos(solarTimePi12th)
		);

	const F32 altitude = F32_atan(
		-F32_cos(declination) * F32_sin(solarTimePi12th) / (
			F32_cos(latitudeRad) * F32_sin(declination) -
			F32_sin(latitudeRad) * F32_cos(declination) * F32_cos(solarTimePi12th)
		)
	);

	return AtmosHelper_equatorialToCartesian(azimuth, altitude, rad);
}

F32x4 AtmosHelper_getSunDir(F64 JD, F32x2 longitudeLatitudeDeg) {
	return AtmosHelper_getSunPosInternal(JD, longitudeLatitudeDeg, 1);
}

F32x4 AtmosHelper_getSunPos(F64 JD, F32x2 longitudeLatitudeDeg) {
	return AtmosHelper_getSunPosInternal(JD, longitudeLatitudeDeg, AtmosHelper_au);
}
