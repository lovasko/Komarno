#include <stdlib.h>
#include <time.h>
#include <stdio.h>
#include <cmath>
#include <vector>
#include <chrono>
#include <SDL/SDL.h>
#include <OpenGL/gl.h>
#include <OpenGL/glu.h>
#include <OpenCL/opencl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#define SWARM_SIZE 10

const unsigned int _SWARM_SIZE = SWARM_SIZE;

typedef struct 
{
	float x;
	float y;

	float
	length ()
	{
		return sqrtf(x*x + y*y);
	}
} vector2;

typedef struct
{
	vector2 position;
	vector2 velocity;
} object;
object swarm[SWARM_SIZE];
object predator;

SDL_Surface *surface;
bool done = false;
bool is_active = true;

cl_context context;
cl_int err;
size_t work_group_size[1];

cl_device_id* devices;
cl_device_id device;
cl_uint num_devices;

cl_platform_id* platforms;
cl_platform_id platform;
cl_uint num_platforms;

cl_command_queue command_queue;
cl_program program;
cl_event event;

cl_kernel rule_1_kernel;
cl_kernel rule_2_kernel;
cl_kernel rule_3_kernel;
cl_kernel rule_4_kernel;
cl_kernel rule_5_kernel;
cl_kernel single_step_kernel;

cl_mem swarm_mem;
cl_mem rule_1_mem;
cl_mem rule_2_mem;
cl_mem rule_3_mem;
cl_mem rule_4_mem;
cl_mem rule_5_mem;
cl_mem new_swarm_mem;
cl_mem predator_mem;

vector2 rule_1_array[SWARM_SIZE];
vector2 rule_2_array[SWARM_SIZE];
vector2 rule_3_array[SWARM_SIZE];
vector2 rule_4_array[SWARM_SIZE];
vector2 rule_5_array[SWARM_SIZE];
object new_swarm[SWARM_SIZE];

void
init_sdl ()
{
	const SDL_VideoInfo *video_info;

	if (SDL_Init( SDL_INIT_VIDEO ) < 0)
	{
		fprintf(stderr, "Video initialization failed: %s", SDL_GetError());
		exit(1);
	}

	video_info = SDL_GetVideoInfo();
	if (!video_info)
	{
		fprintf(stderr, "Video info query failed: %s",
		SDL_GetError());
		exit(1);
	}

	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
	SDL_GL_SetAttribute(SDL_GL_SWAP_CONTROL, 0);

	surface = SDL_SetVideoMode(600, 600, 32, SDL_OPENGL | SDL_GL_DOUBLEBUFFER); 
	if (!surface)
	{
		fprintf(stderr, "Video mode set failed: %s", SDL_GetError());
		exit(1);
	}
}

void
resize_viewport ()
{
  glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	glOrtho(0, 600, 600, 0, -1, 1);
	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
	glDisable(GL_DEPTH_TEST);
}

void
init_opengl ()
{
	glShadeModel(GL_SMOOTH);
	glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
	glClearDepth(20.0f);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

	resize_viewport();
}

bool
platform_selection ()
{
	err = clGetPlatformIDs (0, NULL, &num_platforms);
	if (num_platforms == 0)
	{
		printf("No platforms.\n");
		return false;
	}
	platforms = new cl_platform_id[num_platforms];
	err = clGetPlatformIDs (num_platforms, platforms, NULL);

	/* let the user to choose the platform */
	printf("Select the platform: \n");
	for (unsigned int i = 0; i < num_platforms; i++)
	{
		char name[1024];
		err = clGetPlatformInfo (platforms[i], CL_PLATFORM_NAME, 1024, &name, NULL);
		printf("%d) %s\n", i+1, name);
	}
	
	int selected_platform;
	scanf("%d", &selected_platform);

	/* check the selection for errors */
	if (selected_platform < 1 || selected_platform > num_platforms)
	{
		printf("Selection failed: not such platform number.\n");
		return false;
	}

	platform = platforms[selected_platform-1];
	return true;
}

bool
device_selection ()
{
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 0, 
	    NULL, &num_devices);
	if (num_devices == 0)
	{
		printf("No devices.\n");
		return false;
	}

	devices = new cl_device_id[num_devices];
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_GPU, 
	    num_devices, devices, NULL);

	/* let the user to choose the device */
	printf("Select the device: \n");
	for (unsigned int i = 0; i < num_devices; i++)
	{
		char name[1024];
		err = clGetDeviceInfo (devices[i], CL_DEVICE_NAME, 1024, &name, NULL);
		printf("%d) %s\n", i+1, name);
	}

	int selected_device;
	scanf("%d", &selected_device);

	/* check the selection for errors */
	if (selected_device < 1 || selected_device > num_devices)
	{
		printf("Selection failed: not such device number.\n");
		return false;
	}

	device = devices[selected_device-1];
	return true;
}

bool
init_cl ()
{
	context = clCreateContext(0, 1, devices, NULL, NULL, &err);
	command_queue = clCreateCommandQueue(context, device, 0, &err);

	return true;
}

bool
build_cl_program (const char* _filename)
{
	int fd = open(_filename, O_RDONLY);
	struct stat stats;
	fstat(fd, &stats);

	errno = 0;
	char* source = (char*)mmap(NULL, stats.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (errno != 0)
	{
		printf("ERROR: %s\n", strerror(errno));
		return false;
	}

	/* build the code */
	program = clCreateProgramWithSource(context, 1, (const char**)&source,
	    (const size_t*)&stats.st_size, &err);
	err = clBuildProgram(program, 0, NULL, NULL, NULL, NULL);

	/* print the build log */
	cl_build_status build_status;
	err = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_STATUS, sizeof(cl_build_status), &build_status, NULL);

	char *build_log;
	size_t ret_val_size;
	err = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &ret_val_size);

	build_log = new char[ret_val_size+1];
	err = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, ret_val_size, build_log, NULL);
	build_log[ret_val_size] = '\0';
	printf("build log: \n %s", build_log);

	return true;
}

bool
extract_kernels ()
{
	rule_1_kernel = clCreateKernel(program, "rule_1", &err);
	rule_2_kernel = clCreateKernel(program, "rule_2", &err);
	rule_3_kernel = clCreateKernel(program, "rule_3", &err);
	rule_4_kernel = clCreateKernel(program, "rule_4", &err);
	rule_5_kernel = clCreateKernel(program, "rule_5", &err);
	single_step_kernel = clCreateKernel(program, "single_step", &err);

	return true;
}

bool
setup_memory ()
{
	swarm_mem = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, 
	    sizeof(object) * SWARM_SIZE, swarm, &err);

	new_swarm_mem = clCreateBuffer(context, CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, 
	    sizeof(object) * SWARM_SIZE, new_swarm, &err);

	rule_1_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(vector2) * SWARM_SIZE, rule_1_array, &err);

	rule_2_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(vector2) * SWARM_SIZE, rule_2_array, &err);

	rule_3_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(vector2) * SWARM_SIZE, rule_3_array, &err);

	rule_4_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(vector2) * SWARM_SIZE, rule_4_array, &err);

	rule_5_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(vector2) * SWARM_SIZE, rule_5_array, &err);

	predator_mem = clCreateBuffer(context, CL_MEM_READ_WRITE|CL_MEM_COPY_HOST_PTR, 
	    sizeof(object), &predator, &err);

	return true;
}

bool
setup_kernel_arguments ()
{
	err = clSetKernelArg(rule_1_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(rule_1_kernel, 1, sizeof(cl_mem), (void *) &rule_1_mem);
	err = clSetKernelArg(rule_1_kernel, 2, sizeof(unsigned int), &_SWARM_SIZE);

	err = clSetKernelArg(rule_2_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(rule_2_kernel, 1, sizeof(cl_mem), (void *) &rule_2_mem);
	err = clSetKernelArg(rule_2_kernel, 2, sizeof(unsigned int), &_SWARM_SIZE);

	err = clSetKernelArg(rule_3_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(rule_3_kernel, 1, sizeof(cl_mem), (void *) &rule_3_mem);
	err = clSetKernelArg(rule_3_kernel, 2, sizeof(unsigned int), &_SWARM_SIZE);

	err = clSetKernelArg(rule_4_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(rule_4_kernel, 1, sizeof(cl_mem), (void *) &rule_4_mem);
	err = clSetKernelArg(rule_4_kernel, 2, sizeof(unsigned int), &_SWARM_SIZE);

	err = clSetKernelArg(rule_5_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(rule_5_kernel, 1, sizeof(cl_mem), (void *) &rule_5_mem);
	err = clSetKernelArg(rule_5_kernel, 2, sizeof(cl_mem), (void *) &predator_mem);
	err = clSetKernelArg(rule_5_kernel, 3, sizeof(unsigned int), &_SWARM_SIZE);

	err = clSetKernelArg(single_step_kernel, 0, sizeof(cl_mem), (void *) &swarm_mem);
	err = clSetKernelArg(single_step_kernel, 1, sizeof(cl_mem), (void *) &rule_1_mem);
	err = clSetKernelArg(single_step_kernel, 2, sizeof(cl_mem), (void *) &rule_2_mem);
	err = clSetKernelArg(single_step_kernel, 3, sizeof(cl_mem), (void *) &rule_3_mem);
	err = clSetKernelArg(single_step_kernel, 4, sizeof(cl_mem), (void *) &rule_4_mem);
	err = clSetKernelArg(single_step_kernel, 5, sizeof(cl_mem), (void *) &rule_5_mem);
	err = clSetKernelArg(single_step_kernel, 6, sizeof(cl_mem), (void *) &new_swarm_mem);

	return true;
}

void
gpu_rule_1 ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, rule_1_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, rule_1_mem, CL_TRUE, 0, 
	    sizeof(vector2) * SWARM_SIZE, &rule_1_array, 0, NULL, &event);
	clReleaseEvent(event);
}

void
gpu_rule_2 ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, rule_2_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, rule_2_mem, CL_TRUE, 0, 
	    sizeof(vector2) * SWARM_SIZE, &rule_2_array, 0, NULL, &event);
	clReleaseEvent(event);
}

void
gpu_rule_3 ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, rule_3_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, rule_3_mem, CL_TRUE, 0, 
	    sizeof(vector2) * SWARM_SIZE, &rule_3_array, 0, NULL, &event);
	clReleaseEvent(event);
}

void
gpu_rule_4 ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, rule_4_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, rule_4_mem, CL_TRUE, 0, 
	    sizeof(vector2) * SWARM_SIZE, &rule_4_array, 0, NULL, &event);
	clReleaseEvent(event);
}

void
gpu_rule_5 ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueWriteBuffer(command_queue, predator_mem, CL_TRUE, 0, 
	    sizeof(object), &predator, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, rule_5_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, rule_5_mem, CL_TRUE, 0, 
	    sizeof(vector2) * SWARM_SIZE, &rule_5_array, 0, NULL, &event);
	clReleaseEvent(event);
}

void
gpu_simple_step ()
{
	err = clEnqueueWriteBuffer(command_queue, swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, swarm, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueNDRangeKernel(command_queue, single_step_kernel, 1, NULL, 
	    work_group_size, NULL, 0, NULL, &event);
	clReleaseEvent(event);

	err = clEnqueueReadBuffer(command_queue, new_swarm_mem, CL_TRUE, 0, 
	    sizeof(object) * SWARM_SIZE, &new_swarm, 0, NULL, &event);
	clReleaseEvent(event);

	for (unsigned int i = 0; i < SWARM_SIZE; i++)
		swarm[i] = new_swarm[i];	
}

void
draw_scene ()
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	for (unsigned int i = 0; i < SWARM_SIZE; i++)
	{
		glLoadIdentity();
		glColor3ub(0, 99, 0);

		glTranslatef(swarm[i].position.x, swarm[i].position.y, 0.0f);
		glRotatef(atan2(swarm[i].velocity.y, swarm[i].velocity.x) * 180.0f / M_PI 
		    + 90.0f, 0.0f, 0.0f, 1.0f);

		glBegin(GL_QUADS);
			glVertex2f(-2.0f, -6.0f);
			glVertex2f( 2.0f, -6.0f);
			glVertex2f( 2.0f,  6.0f);
			glVertex2f(-2.0f,  6.0f);
		glEnd();
	}

	glLoadIdentity();
	glColor3ub(139, 0, 0);

	glTranslatef(predator.position.x, predator.position.y, 0.0f);
	glRotatef(atan2(predator.velocity.y, predator.velocity.x) * 180.0f / M_PI 
	    + 90.0f, 0.0f, 0.0f, 1.0f);

	glBegin(GL_QUADS);
		glVertex2f(-3.0f, -8.0f);
		glVertex2f( 3.0f, -8.0f);
		glVertex2f( 3.0f,  8.0f);
		glVertex2f(-3.0f,  8.0f);
	glEnd();
}

vector2
operator- (vector2 const& _a, vector2 const& _b)
{
	vector2 result;
	result.x = _a.x - _b.x;
	result.y = _a.y - _b.y;

	return result;
}

vector2&
operator/= (vector2& _v, float _s)
{
  _v.x /= _s;
  _v.y /= _s;

  return _v;
}

vector2&
operator+= (vector2 & _a, vector2 const& _b)
{
  _a.x += _b.x;
  _a.y += _b.y;

  return _a;
}

vector2
hunt ()
{
	vector2 closest = predator.position - swarm[0].position;

	for (unsigned int i = 0; i < SWARM_SIZE; i++)
	{
		if ((predator.position - swarm[i].position).length() < closest.length())
			closest = (predator.position - swarm[i].position);
	}

	closest /= -35.0f;

	return closest;
}

void
step ()
{
	hunt();

	gpu_rule_1();
	gpu_rule_2();
	gpu_rule_3();
	gpu_rule_4();
	gpu_rule_5();
	gpu_simple_step();
}

void
main_loop ()
{
	is_active = true;
	SDL_Event event;

	while (!done)
	{
		while (SDL_PollEvent(&event))
		{
			switch (event.type)
			{
				case SDL_ACTIVEEVENT:
					if (event.active.state == SDL_APPACTIVE )
						is_active = (event.active.gain != 0);
				break;

				case SDL_QUIT:
					done = true; 
				break;

				default:
				break;
			}
		}

			
		if (is_active)
		{
			step();

			predator.velocity += hunt();
			if (predator.velocity.length() > 0.2f)
				predator.velocity /= 10.0f;
			predator.position += predator.velocity;

			draw_scene();
			SDL_GL_SwapBuffers();
		}
	}
}

int 
main (int argc, char *argv[])
{
	work_group_size[0] = SWARM_SIZE;

	srand(time(NULL));

	for (unsigned int i = 0; i < SWARM_SIZE; i++)
	{
		swarm[i].position.x = rand() % 600;
		swarm[i].position.y = rand() % 600;

		swarm[i].velocity.x = (float)(rand() % 1000) / 1000.0f - 0.5f;
		swarm[i].velocity.y = (float)(rand() % 1000) / 1000.0f - 0.5f;
	}

	if (!platform_selection())
		return 1;

	if (!device_selection())
		return 1;

	if (!init_cl())
		return 1;

	if (!build_cl_program("source.cl"))
		return 1;
	
	if (!extract_kernels())
		return 1;

	if (!setup_memory())
		return 1;

	if (!setup_kernel_arguments())
		return 1;

  init_sdl();
	init_opengl();

	main_loop();
	
	return EXIT_SUCCESS;	
}

