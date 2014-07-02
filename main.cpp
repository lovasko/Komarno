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

SDL_Surface *surface;
bool done = false;
bool is_active = true;

cl_context context;
cl_int err;

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

class Vector2
{
	public:
		Vector2 (float _x = 0.0f, float _y = 0.0f)
		{
			x = _x;
			y = _y;
		}

		float
		length ()
		{
			return sqrtf(x*x + y*y);
		}

		float x;
		float y;	
};

class Mosquito
{
	public:
		Vector2 velocity;
		Vector2 position;

		static Mosquito
		random ()
		{
			Mosquito m;

			m.velocity.x = (float)(rand() % 1000) / 1000.0f - 0.5f;
			m.velocity.y = (float)(rand() % 1000) / 1000.0f - 0.5f;

			if (rand() % 2 == 0)
			{
				m.position.x = (float)(rand() % 300);
				m.position.y = (float)(rand() % 300);
			}
			else
			{
				m.position.x = (float)(rand() % 300) + 300;
				m.position.y = (float)(rand() % 300) + 300;
			}

			return m;
		}

		void
		draw ()
		{
			glLoadIdentity();
			glColor3ub(0, 99, 0);

			glTranslatef(position.x, position.y, 0.0f);
			glRotatef(atan2(velocity.y, velocity.x) * 180.0f / M_PI + 90.0f, 0.0f, 0.0f, 1.0f);

			glBegin(GL_QUADS);
				glVertex2f(-2.0f, -6.0f);
				glVertex2f( 2.0f, -6.0f);
				glVertex2f( 2.0f,  6.0f);
				glVertex2f(-2.0f,  6.0f);
			glEnd();
		}

};

class Dragonfly
{
	public:
		Vector2 velocity;
		Vector2 position;

		static Dragonfly 
		random ()
		{
			Dragonfly d;

			d.velocity.x = (float)(rand() % 1000) / 1000.0f - 0.5f;
			d.velocity.y = (float)(rand() % 1000) / 1000.0f - 0.5f;
			d.position.x = (float)(rand() % 600);
			d.position.y = (float)(rand() % 600);

			return d;
		}

		void
		draw ()
		{
			glLoadIdentity();
			glColor3ub(111, 0, 0);

			glTranslatef(position.x, position.y, 0.0f);
			glRotatef(atan2(velocity.y, velocity.x) * 180.0f / M_PI + 90.0f, 0.0f, 0.0f, 1.0f);

			glBegin(GL_QUADS);
				glVertex2f(-4.0f, -8.0f);
				glVertex2f( 4.0f, -8.0f);
				glVertex2f( 4.0f,  8.0f);
				glVertex2f(-4.0f,  8.0f);
			glEnd();
		}
};

Vector2
operator+ (Vector2 const& _a, Vector2 const& _b)
{
	Vector2 result;
	result.x = _a.x + _b.x;
	result.y = _a.y + _b.y;

	return result;
}

Vector2
operator- (Vector2 const& _a, Vector2 const& _b)
{
	Vector2 result;
	result.x = _a.x - _b.x;
	result.y = _a.y - _b.y;

	return result;
}

Vector2&
operator/= (Vector2& _v, float _s)
{
  _v.x /= _s;
  _v.y /= _s;

  return _v;
}

Vector2&
operator+= (Vector2 & _a, Vector2 const& _b)
{
  _a.x += _b.x;
  _a.y += _b.y;

  return _a;
}

Vector2&
operator-= (Vector2& _a, Vector2& _b)
{
  _a.x -= _b.x;
  _a.y -= _b.y;

  return _a;
}

Vector2
rule_1 (Mosquito& _m, std::vector<Mosquito> _swarm)
{
	Vector2 mass_centre;

	for (auto& m : _swarm)
	{
		if (&_m != &m)
			mass_centre += m.position;
	}
	mass_centre /= (_swarm.size() - 1);

	Vector2 direction = mass_centre - _m.position;	
	direction /= 50.0f;

	return direction;
}

Vector2
rule_2 (Mosquito& _m, std::vector<Mosquito> _swarm)
{
	Vector2 centre;

	for(auto& m : _swarm)
	{
		if (&_m != &m)
		{
			Vector2 difference = m.position - _m.position;
			if (difference.length() < 20.0f)
				centre -= difference;
		}
	}

	return centre;
}

Vector2
rule_3 (Mosquito& _m, std::vector<Mosquito> _swarm)
{
	Vector2 velocity;

	for(auto& m : _swarm)
	{
		if (&_m != &m)
		{
			velocity += m.velocity;
		}
	}
	velocity /= (float)(_swarm.size() - 1);

	Vector2 result;
	result = velocity - _m.velocity;
	result /= 2.0f;

	return result;
}

Vector2
rule_4 (Mosquito& _m)
{
	Vector2 top_velocity;
	Vector2 bottom_velocity;
	Vector2 left_velocity;
	Vector2 right_velocity;

	if (_m.position.x == 0.0f || _m.position.y == 0.0f ||
	    _m.position.x == 600.0f || _m.position.y == 600.0f)
		return Vector2();

	top_velocity.y = fabs(20.0f / _m.position.y);	
	bottom_velocity.y = -fabs(20.0f / (_m.position.y - 600.0f));	
	left_velocity.x = fabs(20.0f / _m.position.x);	
	right_velocity.x = -fabs(20.0f / (_m.position.x - 600.0f));	

	Vector2 result = top_velocity + bottom_velocity + left_velocity +
	    right_velocity;

	result /= 0.1f;

	return result;
}

Vector2
rule_5 (Mosquito& _m, Dragonfly& _d)
{
	Vector2 result = _m.position - _d.position;
	result /= 60.0;

	return result;
}

Vector2
hunt (Dragonfly& _d, std::vector<Mosquito>& _swarm)
{
	Vector2 closest = _d.position - _swarm[0].position;

	for (auto& m : _swarm)
		if ((_d.position - m.position).length() < closest.length())
			closest = (_d.position - m.position);

	closest /= -35.0f;

	return closest;
}

void
draw_scene (std::vector<Mosquito>& _swarm, Dragonfly& _dragonfly)
{
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
	glClearColor(1.0f, 1.0f, 1.0f, 1.0f);

	for (auto& m : _swarm)
		m.draw();

	_dragonfly.draw();
}

std::vector<Mosquito>
step (std::vector<Mosquito>& _swarm, Dragonfly& _dragonfly)
{
	std::vector<Mosquito> new_swarm;

	for (auto& m : _swarm)
	{
		Vector2 v1 = rule_1(m, _swarm);
		Vector2 v2 = rule_2(m, _swarm);
		Vector2 v3 = rule_3(m, _swarm);
		Vector2 v4 = rule_4(m);
		Vector2 v5 = rule_5(m, _dragonfly);

		Vector2 velocity;
		velocity += v1;
		velocity += v2;
		velocity += v3;
		velocity += v4;
		velocity += v5;

		velocity /= 10000.0f;

		Mosquito new_mosquito;
		new_mosquito.velocity = m.velocity + velocity;
		new_mosquito.position = m.position + new_mosquito.velocity;

		if (new_mosquito.velocity.length() > 0.6)
			new_mosquito.velocity /= 10.0f;

		new_swarm.push_back(new_mosquito);
	}

	return new_swarm;
}

void
main_loop (std::vector<Mosquito>& _swarm, Dragonfly& _dragonfly)
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
			_swarm = step(_swarm, _dragonfly);
			_dragonfly.velocity += hunt(_dragonfly, _swarm);
			if (_dragonfly.velocity.length() > 0.2)
				_dragonfly.velocity /= 10.0f;
			_dragonfly.position += _dragonfly.velocity;

			draw_scene(_swarm, _dragonfly);
			SDL_GL_SwapBuffers();
		}
	}
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
	rule_6_kernel = clCreateKernel(program, "rule_5", &err);
	single_step_kernel = clCreateKernel(program, "single_step", &err);
}

int 
main (int argc, char *argv[])
{
	const unsigned int N = 20;
	std::vector<Mosquito> swarm;
	srand(time(NULL));

	for (unsigned int i = 0; i < N; i++)
		swarm.push_back(Mosquito::random());	

	Dragonfly dragonfly = Dragonfly::random();

	if (!platform_selection())
		return 1;

	if (!device_selection())
		return 1;

	if (!init_cl())
		return 1;

	if (!build_cl_program("source.cl"))
		return 1;
	
	if(!extract_kernels())
		return 1;

  init_sdl();
	init_opengl();

	main_loop(swarm, dragonfly);
	
	return EXIT_SUCCESS;	
}

	if (!init_cl())
	if (!init_cl())
		return 1;

		return 1;

