#include <CL/cl.h>
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>

#define KERNEL(NAME, ...) const char* NAME = #__VA_ARGS__

KERNEL(mb_kern,

	typedef struct {
		int w, h;
		float xo, yo;
		float zoom;
		int max_iter;
		char axes;
		unsigned int palette[5];
		float N;
	} MB;

	__kernel void mb(__global uint* out, __constant MB* mb) {

		// pixel coordinates
		int x = get_global_id(0);
		int y = get_global_id(1);

		// complex view width and coordinates
		float cd = mb->zoom / mb->w;
		float cx = (2 * x - mb->w) * cd + mb->xo;
		float cy = (mb->h - 2 * y) * cd + mb->yo;

		// draw axes overlay
		if (mb->axes) {
			if (
				// axes
				fabs(cx) < cd ||
				fabs(cy) < cd ||
				// major ticks
				round(cx) != 0 && fabs(cx - round(cx)) < 4 * cd && fabs(cy) < 16 * cd ||
				round(cy) != 0 && fabs(cy - round(cy)) < 4 * cd && fabs(cx) < 16 * cd ||
				// minor ticks
				round(4 * cx) != 0 && fabs(4 * cx - round(4 * cx)) < 4 * cd && fabs(cy) < 8 * cd ||
				round(4 * cy) != 0 && fabs(4 * cy - round(4 * cy)) < 4 * cd && fabs(cx) < 8 * cd
			) {
				out[y * mb->w + x] = 0xFFFF5511;
				return;
			}
		}

		float zx = 0, zy = 0;
		for (int iter = 0; iter < mb->max_iter; iter++) {

			if (mb->N == 2) {
				// calculation shortcut for default exponent
				float tmp = 2 * zx * zy;
				zx = zx * zx - zy * zy + cx;
				zy = tmp + cy;
			} else {
				float rN = native_powr(zx * zx + zy * zy, mb->N / 2);
				float thetaN = atan2(zy, zx) * mb->N;
				zx = rN * native_cos(thetaN) + cx;
				zy = rN * native_sin(thetaN) + cy;
			}

			if (zx * zx + zy * zy > 256) {

				// smooth coloring
				float val = iter - native_log2(native_log2(zx * zx + zy * zy)) / native_log2(mb->N);

				// cycle colors more slowly
				val /= 8;

				// interpolate palette colors
				float f_floor;
				float frac = fract(val, &f_floor);
				int i_floor = convert_int(f_floor);
				uint col_a = mb->palette[((i_floor % 5) + 5) % 5];
				uint col_b = mb->palette[((i_floor % 5) + 6) % 5];

				uchar r = ((1 - frac) * ((col_a >> 16) & 0xFF) + frac * ((col_b >> 16) & 0xFF));
				uchar g = ((1 - frac) * ((col_a >>  8) & 0xFF) + frac * ((col_b >>  8) & 0xFF));
				uchar b = ((1 - frac) * ((col_a      ) & 0xFF) + frac * ((col_b      ) & 0xFF));

				out[y * mb->w + x] = 0xFF000000 + (r << 16) + (g << 8) + b;
				return;
			}
		}

		out[y * mb->w + x] = 0xFF000000;
	}

);

int main(int argc, char** argv) {

	typedef struct {
		int w, h;
		float xo, yo;
		float zoom;
		int max_iter;
		char axes;
		unsigned int palette[5];
		float N;
	} MB;

	MB mb = {
		1600, 900,
		0, 0,
		2.5,
		1000,
		0,
		{0xFF000000, 0xFF0055FF, 0xFFDDDDDD, 0xFFFF9000, 0xFF643200},
		2.0
	};

	/*/
	/* Initialize OpenCL
	/*/

	cl_platform_id platform_id;
	clGetPlatformIDs(1, &platform_id, NULL);

	cl_device_id device_id;
	clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_GPU, 1, &device_id, NULL);

	cl_context context;
	context = clCreateContext(NULL, 1, &device_id, NULL, NULL, NULL);

	cl_command_queue queue;
	queue = clCreateCommandQueueWithProperties(context, device_id, NULL, NULL);

	char* kernel_source = strdup(mb_kern);
	size_t kernel_size = strlen(kernel_source);

	cl_program program;
	program = clCreateProgramWithSource(context, 1, (const char**)&kernel_source, &kernel_size, NULL);
	cl_int ret = clBuildProgram(program, 1, &device_id, NULL, NULL, NULL);

	free(kernel_source);

	// check for kernel compilation error
	if (ret == -11) {
		// get and print error message
		size_t len;
		cl_int ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, 0, NULL, &len);
		char* log = (char*)malloc(len);
		ret = clGetProgramBuildInfo(program, device_id, CL_PROGRAM_BUILD_LOG, len, log, NULL);
		printf("%d %s\n", ret, log);
		exit(1);
	}

	cl_kernel kernel;
	kernel = clCreateKernel(program, "mb", NULL);

	cl_mem memobj_out;
	memobj_out = clCreateBuffer(context, CL_MEM_WRITE_ONLY, mb.w * mb.h * 4, NULL, NULL);
	clSetKernelArg(kernel, 0, sizeof(cl_mem), &memobj_out);

	cl_mem memobj_mb;
	memobj_mb = clCreateBuffer(context, CL_MEM_READ_ONLY, sizeof(MB), NULL, NULL);
	clSetKernelArg(kernel, 1, sizeof(cl_mem), &memobj_mb);

	/*/
	/* Initialize SDL
	/*/

	SDL_Init(SDL_INIT_VIDEO);

	SDL_Window* window;
	window = SDL_CreateWindow(
		"",
		SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		mb.w, mb.h,
		SDL_WINDOW_SHOWN
	);

	SDL_Surface* surface;
	surface = SDL_GetWindowSurface(window);

	/*/
	/* Main Loop
	/*/

	for (char render = 1, zooming = 0; ; ) {

		SDL_Event e;
		while (SDL_PollEvent(&e)) {

			switch (e.type) {

				case SDL_QUIT:
					return 0;

				case SDL_MOUSEMOTION:
					// mouse drag
					if (e.motion.state & SDL_BUTTON_LMASK) {
						mb.xo -= e.motion.xrel * mb.zoom / mb.w * 2;
						mb.yo += e.motion.yrel * mb.zoom / mb.w * 2;
						render = 1;
					}
					break;

				case SDL_MOUSEWHEEL:
					// smooth zoom over 8 steps
					zooming = e.wheel.y > 0 ? 8 : -8;
					break;

				case SDL_KEYDOWN:
					{
						SDL_Keycode key = e.key.keysym.sym;
						switch (key) {
							case SDLK_ESCAPE:
								// quit
								return 0;
							case SDLK_SPACE:
								// show axes
								mb.axes = !mb.axes;
								render = 1;
								break;
							case SDLK_KP_PLUS:
							case SDLK_PAGEUP:
								// increase exponent
								mb.N += 0.05;
								render = 1;
								break;
							case SDLK_KP_MINUS:
							case SDLK_PAGEDOWN:
								// decrease exponent
								mb.N -= 0.05;
								if (mb.N < 1.0) mb.N = 1;
								render = 1;
								break;
							default:
								// jump to specific exponent on number key
								if (SDLK_1 <= key && key <= SDLK_9) {
									mb.N = key - SDLK_1 + 1;
									render = 1;
								} else if (SDLK_KP_1 <= key && key <= SDLK_KP_9) {
									mb.N = key - SDLK_KP_1 + 1;
									render = 1;
								}
						}
					}
					break;
			}
		}

		if (zooming) {
			int mx, my;
			SDL_GetMouseState(&mx, &my);

			float zoom_f = 1.07;
			mb.xo += (2.0 * mx / mb.w - 1) * mb.zoom * (1 - (zooming < 0 ? zoom_f : 1 / zoom_f));
			mb.yo += (mb.h - 2.0 * my) / mb.w * mb.zoom * (1 - (zooming < 0 ? zoom_f : 1 / zoom_f));
			mb.zoom *= zooming < 0 ? zoom_f : 1 / zoom_f;
			zooming += zooming < 0 ? 1 : -1;

			render = 1;
		}

		if (render) {
			size_t global_size[2] = {mb.w, mb.h};
			clEnqueueWriteBuffer(queue, memobj_mb, CL_FALSE, 0, sizeof(MB), &mb, 0, NULL, NULL);
			clEnqueueNDRangeKernel(queue, kernel, 2, NULL, global_size, NULL, 0, NULL, NULL);
			clEnqueueReadBuffer(queue, memobj_out, CL_TRUE, 0, mb.w * mb.h * 4, surface->pixels, 0, NULL, NULL);
			SDL_UpdateWindowSurface(window);

			char title[100];
			sprintf(title, "X:%f Y:%f Z:%f N:%.2f", mb.xo, mb.yo, mb.zoom, mb.N);
			SDL_SetWindowTitle(window, title);

			render = 0;
		}

		SDL_Delay(10);
	}

}
