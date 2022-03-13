#include "math/camera.h"
#include "types/bit_helper.h"
#include "types/timer.h"
#include "types/assert_helper.h"
#include <stdio.h>

void *ourAlloc(void *allocator, usz siz) {
	allocator;
	return malloc(siz);
}

void ourFree(void *allocator, struct Buffer buf) {
	allocator;
	free(buf.ptr);
}

//TODO: Wrap this

#pragma pack(push, 1)

	struct BMPHeader {
		u16 fileType;
		u32 fileSize;
		u16 r0, r1;
		u32 offsetData;
	};
	
	struct BMPInfoHeader {
		u32 size;
		i32 width, height;
		u16 planes, bitCount;
		u32 compression, compressedSize;
		i32 xPixPerM, yPixPerM;
		u32 colorsUsed, colorsImportant;
	};

	struct BMPColorHeader {
		u32 redMask, greenMask, blueMask, alphaMask;
		u32 colorSpaceType;
		u32 unused[16];
	};

#pragma pack(pop)

const u16 BMP_magic = 0x4D42;
const u32 BMP_srgbMagic = 0x73524742;

struct Buffer BMP_write(struct Buffer buf, u16 w, u16 h) {

	ocAssert("BMP can only be up to 4GiB", buf.siz <= u32_MAX);

	u32 headersSize = (u32) (
		sizeof(struct BMPHeader) + 
		sizeof(struct BMPInfoHeader) + 
		sizeof(struct BMPColorHeader)
	);

	struct BMPHeader header = (struct BMPHeader) {
		.fileType = BMP_magic,
		.fileSize = (u32) buf.siz,
		.r0 = 0, .r1 = 0, 
		.offsetData = headersSize
	};

	struct BMPInfoHeader infoHeader = (struct BMPInfoHeader) {
		.size = sizeof(struct BMPInfoHeader),
		.width = w,
		.height = h,
		.planes = 1,
		.bitCount = 32,
		.compression = 3,		//rgba8
		.compressedSize = 0,
		.xPixPerM = 0, .yPixPerM = 0,
		.colorsUsed = 0, .colorsImportant = 0
	};

	struct BMPColorHeader colorHeader = (struct BMPColorHeader) {

		.redMask	= (u32) 0xFF << 16,
		.greenMask	= (u32) 0xFF << 8,
		.blueMask	= (u32) 0xFF,
		.alphaMask	= (u32) 0xFF << 24,

		.colorSpaceType = BMP_srgbMagic
	};

	struct Buffer file = Bit_bytes(
		headersSize + buf.siz,
		ourAlloc,
		NULL
	);

	struct Buffer fileAppend = file;

	Bit_append(&fileAppend, &header, sizeof(header));
	Bit_append(&fileAppend, &infoHeader, sizeof(infoHeader));
	Bit_append(&fileAppend, &colorHeader, sizeof(colorHeader));
	Bit_appendBuffer(&fileAppend, buf);

	return file;
}

void File_write(struct Buffer buf, const c8 *loc) {

	ocAssert("Invalid file name or buffer", loc && buf.siz && buf.ptr);

	FILE *f = fopen(loc, "wb");
	ocAssert("File not found", f);

	fwrite(buf.ptr, 1, buf.siz, f);
	fclose(f);

}

int main(int argc, const char *argv[]) {

	argc; argv;

	//Init camera

	u16 renderWidth = 3840, renderHeight = 2560;

	quat dir = Quat_identity();
	f32x4 origin = Vec_zero();
	struct Camera cam = Camera_init(dir, origin, 80, .01f, 1000.f, renderWidth, renderHeight);

	usz renderPixels = (usz)renderWidth * renderHeight;

	u32 skyColor = 0xFF0080FF;

	const c8 *outputBmp = "output.bmp";

	//Init spheres

	Sphere sph[4] = { 
		Sphere_init(Vec_init3(5, -2, 0), 1), 
		Sphere_init(Vec_init3(5, 0, -2), 1), 
		Sphere_init(Vec_init3(5, 0, 2),  1), 
		Sphere_init(Vec_init3(5, 2, 0),  1)
	};

	u32 sphCols[4] = { 0xFFFF8000, 0xFF808000, 0xFF8000FF, 0xFF800000 };

	//Output to screen

	struct Buffer buf = Bit_bytes(renderPixels << 2, ourAlloc, NULL);
	struct Buffer appendBuf = buf;

	ns start = Timer_now();

	struct Ray r;
	struct Intersection inter;

	for (u16 j = 0; j < renderHeight; ++j)
		for (u16 i = 0; i < renderWidth; ++i) {

			Camera_genRay(&cam, &r, i, j, renderWidth, renderHeight);

			Intersection_init(&inter);

			for (usz k = 0; k < sizeof(sph) / sizeof(sph[0]); ++k)
				Sphere_intersect(sph[k], r, &inter, (u32) k);

			if (!i || !j || i == renderWidth - 1 || j == renderHeight - 1) {
				Bit_appendU32(&appendBuf, (i & 1 ? 0xFFFF0000 : 0xFF7F0000) | (j & 1 ? 0xFF : 0x7F));
				continue;
			}

			if (inter.hitT < 0) {
				Bit_appendU32(&appendBuf, skyColor);
				continue;
			}

			Bit_appendU32(&appendBuf, sphCols[inter.object]);
		}

	struct Buffer file = BMP_write(buf, renderWidth, renderHeight);
	File_write(file, outputBmp);
	Bit_free(&file, ourFree, NULL);

	printf("Finished in %fms\n", (f32)Timer_elapsed(start) / ms);

	Bit_free(&buf, ourFree, NULL);
	return 0;
}