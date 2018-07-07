#include "pipeline.hpp"

#include <glm/gtx/norm.hpp>
#include <glm/gtx/hash.hpp>

#include <iostream>
#include <unordered_map>
#include <unordered_set>


void ak::peel_chains(
	ak::Parameters const &parameters,
	ak::Model const &model, //in: model
	std::vector< float > const &times,          //in: time field (times @ vertices)
	std::vector< std::vector< ak::EmbeddedVertex > > const &active_chains, //in: current active chains
	std::vector< std::vector< ak::EmbeddedVertex > > *next_chains_, //out: next chains (may be different size than active_chains)
	Model *DEBUG_clipped_model
) {
	assert(next_chains_);
	auto &next_chains = *next_chains_;
	next_chains.clear();

	{
		//DEBUG:
		uint32_t loops = 0;
		uint32_t lines = 0;
		for (auto const &chain : active_chains) {
			if (chain[0] == chain.back()) ++loops;
			else ++lines;
		}
		std::cout << "---- peel chains on [" << loops << " loops and " << lines << " lines] ----" << std::endl;
	}

	Model clipped;
	std::vector< ak::EmbeddedVertex > clipped_vertices;
	ak::trim_model(model, active_chains, std::vector< std::vector< ak::EmbeddedVertex > >(), &clipped, &clipped_vertices);
	if (DEBUG_clipped_model) {
		*DEBUG_clipped_model = clipped;
	}

	//This version of the code just uses the 3D distance to the curve.
	//might have problems with models that get really close to themselves.

	std::vector< float > values(clipped.vertices.size(), std::numeric_limits< float >::infinity());

	auto do_seg = [&values,&clipped](glm::vec3 const &a, glm::vec3 const &b) {
		if (a == b) return;
		glm::vec3 ab = b-a;
		float limit = glm::dot(ab,ab);
		float inv_limit = 1.0f / limit;
		for (auto const &v : clipped.vertices) {
			float amt = glm::dot(v-a, ab);
			amt = std::max(0.0f, std::min(limit, amt));
			glm::vec3 pt = (amt * inv_limit) * (b-a) + a;
			float dis2 = glm::length2(v - pt);
			float &best2 = values[&v - &clipped.vertices[0]];
			best2 = std::min(best2, dis2);
		}
	};

	for (auto const &chain : active_chains) {
		for (uint32_t i = 0; i + 1 < chain.size(); ++i) {
			do_seg(chain[i].interpolate(model.vertices), chain[i+1].interpolate(model.vertices));
		}
	}

	for (auto &v : values) {
		v = std::sqrt(v);
	}

	//TODO: blur/smooth distance fn somehow?

	//extract chains:

	//for (int step = 1; step < 10; ++step) { //DEBUG: show several distances
	{	int step = 1;

		float level = step * (2.0f * parameters.stitch_height_mm / parameters.model_units_mm);

		std::vector< std::vector< EmbeddedVertex > > level_chains;

		ak::extract_level_chains(clipped, values, level, &level_chains);

		uint32_t loops = 0;
		uint32_t lines = 0;

		for (auto &chain : level_chains) {
			//chain is embedded on 'clipped' which is embedded on 'model'; re-embed on just 'model':
			for (auto &v : chain) {
				glm::uvec3 simplex = clipped_vertices[v.simplex.x].simplex;
				if (v.simplex.y != -1U) simplex = ak::EmbeddedVertex::common_simplex(simplex, clipped_vertices[v.simplex.y].simplex);
				if (v.simplex.z != -1U) simplex = ak::EmbeddedVertex::common_simplex(simplex, clipped_vertices[v.simplex.z].simplex);
				glm::vec3 weights = v.weights.x * clipped_vertices[v.simplex.x].weights_on(simplex);
				if (v.simplex.y != -1U) weights += v.weights.y * clipped_vertices[v.simplex.y].weights_on(simplex);
				if (v.simplex.z != -1U) weights += v.weights.z * clipped_vertices[v.simplex.z].weights_on(simplex);
				v.simplex = simplex;
				v.weights = weights;
			}

			//subdivide chain and add to outputs:
			if (chain[0] == chain.back()) ++loops;
			else ++lines;
			next_chains.emplace_back();
			sample_chain(parameters.get_chain_sample_spacing(), model, chain, &next_chains.back());
		}
		std::cout << "  extracted " << loops << " loops and " << lines << " lines." << std::endl;
	}

}

