#include <stdlib.h>
#include <cmath>
#include <vector>

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
operator+= (Vector2& _a, Vector2& _b)
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
	direction /= 100.0f;

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
			if (difference.length() < 100.0f)
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
	result /= 8.0f;

	return result;
}

int 
main (int argc, char **argv)
{
	return EXIT_SUCCESS;	
}

