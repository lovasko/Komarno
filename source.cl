typedef struct 
{
	float2 position;
	float2 velocity;
} mosquito;

typedef mosquito dragonfly;

__kernel void 
rule_1 (__global mosquito* _swarm, __global float2* _mass_centre, 
    const unsigned int _swarm_size)
{
	unsigned int idx = get_global_id(0);
	float2 mass_centre = (float2)(0.0f, 0.0f);

	for (unsigned int i = 0; i < _swarm_size; i++)
	{
		if (i == idx) continue;
		mass_centre += _swarm[i].position;
	}

	mass_centre /= (float)(_swarm_size - 1);
	_mass_centre[idx] = mass_centre;
}

__kernel void
rule_2 (__global mosquito* _swarm, __global float2 *_centre, 
    const unsigned int _swarm_size)
{
	unsigned int idx = get_global_id(0);
	float2 centre = (float2)(0.0f, 0.0f);

	for (unsigned int i = 0; i < _swarm_size; i++)
	{
		if (i == idx) continue;
		float2 difference = _swarm[i].position - _swarm[idx].position;
		if (fast_length(difference) < 20.0f)
			centre -= difference;
	}

	_centre[idx] = centre;
}

__kernel void
rule_3 (__global mosquito* _swarm, __global float2 *_velocity, 
    const unsigned int _swarm_size)
{
	unsigned int idx = get_global_id(0);
	float2 velocity = (float2)(0.0f, 0.0f);

	for (unsigned int i = 0; i < _swarm_size; i++)
	{
		if (i == idx) continue;
		velocity += _swarm[i].velocity;
	}
	velocity /= (float)(_swarm_size - 1);
	velocity = _swarm[idx].velocity - velocity;
	velocity /= 2.0f;

	_velocity[idx] = velocity;
}

__kernel void
rule_4 (__global mosquito* _swarm, __global float2 *_border_force,
    const unsigned int _swarm_size)
{
	unsigned int idx = get_global_id(0);
	float2 top_velocity = (float2)(0.0f, 0.0f);
	float2 bottom_velocity = (float2)(0.0f, 0.0f);
	float2 left_velocity = (float2)(0.0f, 0.0f);
	float2 right_velocity = (float2)(0.0f, 0.0f);

	if (_swarm[idx].position.x == 0.0f 
	 || _swarm[idx].position.y == 0.0f 
	 || _swarm[idx].position.x == 600.0f 
	 || _swarm[idx].position.y == 600.0f)
	{
		_border_force[idx] = (float2)(0.0f, 0.0f);
		return;
	}

	top_velocity.y = fabs(20.0f / _swarm[idx].position.y);	
	bottom_velocity.y = -fabs(20.0f / (_swarm[idx].position.y - 600.0f));	
	left_velocity.x = fabs(20.0f / _swarm[idx].position.x);	
	right_velocity.x = -fabs(20.0f / (_swarm[idx].position.x - 600.0f));	

	float2 result = top_velocity + bottom_velocity + left_velocity +
	    right_velocity;
	result /= 0.1f;

	_border_force[idx] = result;
}

__kernel void
rule_5 (__global mosquito* _swarm, __global float2 *_fear,
    __global dragonfly *_predator, const unsigned int _swarm_size)
{
	unsigned int idx = get_global_id(0);
	float2 result = _swarm[idx].position - _predator->position;
	result /= 60.0f;

	_fear[idx] = result;
}

__kernel void
single_step (__global mosquito* _swarm, __global float2* _rule_1, 
    __global float2* _rule_2, __global float2* _rule_3, 
    __global float2* _rule_4, __global float2* _rule_5, 
    __global mosquito* _new_swarm)
{
	unsigned int idx = get_global_id(0);
	float2 velocity = _rule_1[idx] + _rule_2[idx] + _rule_3[idx] + _rule_4[idx]
	    + _rule_5[idx];
	velocity /= 10000.0f;

	_new_swarm[idx].velocity = _swarm[idx].velocity + velocity;
	_new_swarm[idx].position = _swarm[idx].position + _swarm[idx].velocity 
	    + velocity;
}

