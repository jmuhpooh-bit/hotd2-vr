/*
	Making the agent's hands and pistol from one render pass (hotd2-vr). See hands_build.h.

	Step for step what hotd2-vr/assets/rip_hands.py does (the comments there say why):
	the pistol, the hand around its grip and the open hand are found by their textures, lifted
	back into 3D with the game's focal length, the pistol is stood upright along its barrel,
	its slide is cut free along the line painted on its sides (with a cap and a barrel
	underneath), and everything is written in metres:

		gun space    the pistol upright, barrel along -z, y up, origin in the fist
		hand space   the open left hand, fingers along -z, palm towards +x, thumb up,
		             origin just off the palm

	Copyright 2026 mikermak. This file is part of Flycast and is distributed under the GNU GPL v2 or later.
*/
#include "hands_build.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <map>
#include <numeric>
#include <tuple>
#include <unordered_map>

namespace vr::hands
{
namespace
{

using glm::dvec2;
using glm::dvec3;

constexpr double Metres = 0.095;		// per game unit
constexpr double SlideTravel = 0.16;	// of the pistol's length
constexpr double PalmOffset = 0.2;		// game units: the controller's grip is this far off the open palm
constexpr double GrabShift[2] { -0.17, 0.02 };	// the open hand on the slide: left and up, of the pistol's length

constexpr uint32_t TspClampV = 1u << 15, TspClampU = 1u << 16, TspFlipV = 1u << 17, TspFlipU = 1u << 18,
		TspIgnoreTexA = 1u << 19;

// A texture as used: tcw, the tsp's size bits, and flags (1 clamp u, 2 clamp v, 4 mirror u,
// 8 mirror v, 16 alpha test). None for untextured parts.
struct Key
{
	bool none = true;
	uint32_t tcw = 0, size = 0, flags = 0;
	bool operator==(const Key& o) const { return none == o.none && tcw == o.tcw && size == o.size && flags == o.flags; }
};

struct Tri
{
	dvec3 p[3];
	dvec2 uv[3];
	uint8_t col[4];
	Key tex;
};
using Tris = std::vector<Tri>;
using Polys = std::vector<const RipPoly *>;

Key textureKey(const RipPoly& poly)
{
	Key k;
	if (!((poly.pcw >> 3) & 1))
		return k;
	const uint32_t tsp = poly.tsp;
	k.none = false;
	k.tcw = poly.tcw;
	k.size = tsp & 0x3F;
	k.flags = (tsp & TspClampU ? 1 : 0) | (tsp & TspClampV ? 2 : 0) | (tsp & TspFlipU ? 4 : 0) | (tsp & TspFlipV ? 8 : 0)
			| (tsp & TspIgnoreTexA ? 0 : 16);
	return k;
}

// Game eye space of a polygon's vertices: game units, y up, looking down -z.
std::vector<dvec3> eye(const Rip& rip, const RipPoly& poly)
{
	std::vector<dvec3> e;
	e.reserve(poly.v.size());
	for (const RipVertex& v : poly.v)
	{
		const float w = 1.f / v.z;
		const float x = (v.x - rip.fbWidth * 0.5f) / rip.focalX * w;
		const float y = -(v.y - rip.fbHeight * 0.5f) / rip.focalY * w;
		e.emplace_back(x, y, -w);
	}
	return e;
}

dvec3 centre(const Rip& rip, const Polys& polys)
{
	dvec3 sum(0.0);
	size_t n = 0;
	for (const RipPoly *p : polys)
		for (const dvec3& e : eye(rip, *p))
		{
			sum += e;
			n++;
		}
	return n ? sum / (double)n : sum;
}

// Triangles in game eye space, each wound so its normal faces out: by the winding the game
// culled with (ISP cull mode 2/3), the sign settled by a vote over all of them (outwards from
// the middle), else outwards from the middle.
Tris triangles(const Rip& rip, const Polys& polys)
{
	struct Item { Tri t; int sign, outward; };
	std::vector<Item> items;
	int votes = 0;
	const dvec3 mid = centre(rip, polys);
	for (const RipPoly *poly : polys)
	{
		const std::vector<dvec3> e = eye(rip, *poly);
		const Key key = textureKey(*poly);
		const int cull = (poly->isp >> 27) & 3;
		for (size_t i = 0; i + 2 < e.size(); i++)
		{
			const size_t a = i % 2 == 0 ? i : i + 1, b = i % 2 == 0 ? i + 1 : i, c = i + 2;
			const dvec3 n = glm::cross(e[b] - e[a], e[c] - e[a]);
			if (glm::length(n) < 1e-12)
				continue;
			const RipVertex *v = poly->v.data();
			const double area = ((double)v[b].x - v[a].x) * ((double)v[c].y - v[a].y) - ((double)v[c].x - v[a].x) * ((double)v[b].y - v[a].y);
			const int sign = (area > 0 ? 1 : -1) * (cull == 2 ? 1 : cull == 3 ? -1 : 0);
			const int outward = glm::dot(n, (e[a] + e[b] + e[c]) / 3.0 - mid) > 0 ? 1 : -1;
			if (sign != 0)
				votes += sign * outward;
			Item item;
			const size_t idx[3] { a, b, c };
			for (int k = 0; k < 3; k++)
			{
				item.t.p[k] = e[idx[k]];
				item.t.uv[k] = dvec2(v[idx[k]].u, v[idx[k]].v);
			}
			std::fill(std::begin(item.t.col), std::end(item.t.col), 255);
			item.t.tex = key;
			item.sign = sign;
			item.outward = outward;
			items.push_back(item);
		}
	}
	const int convention = votes >= 0 ? 1 : -1;
	Tris tris;
	tris.reserve(items.size());
	for (Item& item : items)
	{
		if ((item.sign != 0 ? item.sign * convention : item.outward) < 0)
		{
			std::swap(item.t.p[1], item.t.p[2]);
			std::swap(item.t.uv[1], item.t.uv[2]);
		}
		tris.push_back(item.t);
	}
	return tris;
}

struct PointKey
{
	long long x, y, z;
	bool operator==(const PointKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct PointHash
{
	size_t operator()(const PointKey& k) const {
		return std::hash<long long>()(k.x * 73856093LL ^ k.y * 19349663LL ^ k.z * 83492791LL);
	}
};
PointKey pointKey(const dvec3& p, double scale) {
	return { std::llrint(p.x * scale), std::llrint(p.y * scale), std::llrint(p.z * scale) };
}

// Polygons grouped into pieces that share vertices, in the order of their first polygon.
std::vector<Polys> clusters(const Rip& rip, const Polys& polys)
{
	std::vector<size_t> parent(polys.size());
	std::iota(parent.begin(), parent.end(), 0);
	auto find = [&](size_t i) {
		while (parent[i] != i)
		{
			parent[i] = parent[parent[i]];
			i = parent[i];
		}
		return i;
	};
	std::unordered_map<PointKey, size_t, PointHash> owner;
	for (size_t i = 0; i < polys.size(); i++)
		for (const dvec3& e : eye(rip, *polys[i]))
		{
			const PointKey k = pointKey(e, 1000.0);
			auto it = owner.find(k);
			if (it != owner.end())
				parent[find(i)] = find(it->second);
			else
				owner.emplace(k, i);
		}
	std::vector<Polys> groups;
	std::unordered_map<size_t, size_t> groupOf;
	for (size_t i = 0; i < polys.size(); i++)
	{
		const size_t root = find(i);
		auto it = groupOf.find(root);
		if (it == groupOf.end())
		{
			groupOf.emplace(root, groups.size());
			groups.push_back({ polys[i] });
		}
		else
			groups[it->second].push_back(polys[i]);
	}
	return groups;
}

dvec3 surfaceCentre(const Tris& tris)
{
	dvec3 sum(0.0);
	double total = 0.0;
	for (const Tri& t : tris)
	{
		const double a = glm::length(glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0]));
		sum += (t.p[0] + t.p[1] + t.p[2]) / 3.0 * a;
		total += a;
	}
	return total > 0.0 ? sum / total : sum;
}

std::vector<dvec3> corners(const Tris& tris)
{
	std::vector<dvec3> pts;
	pts.reserve(tris.size() * 3);
	for (const Tri& t : tris)
		pts.insert(pts.end(), std::begin(t.p), std::end(t.p));
	return pts;
}

dvec3 mean(const std::vector<dvec3>& pts)
{
	dvec3 sum(0.0);
	for (const dvec3& p : pts)
		sum += p;
	return pts.empty() ? sum : sum / (double)pts.size();
}

// The principal axes of points around their middle (as the rows of numpy's SVD): largest
// spread first. A symmetric 3x3 eigen decomposition (Jacobi); the signs are arbitrary and
// everything using them settles its own.
void principalAxes(const std::vector<dvec3>& pts, const dvec3& mid, dvec3 axes[3])
{
	double a[3][3] {};
	for (const dvec3& p : pts)
	{
		const dvec3 d = p - mid;
		for (int i = 0; i < 3; i++)
			for (int j = 0; j < 3; j++)
				a[i][j] += d[i] * d[j];
	}
	double v[3][3] { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
	for (int sweep = 0; sweep < 64; sweep++)
	{
		const double off = a[0][1] * a[0][1] + a[0][2] * a[0][2] + a[1][2] * a[1][2];
		const double diag = a[0][0] * a[0][0] + a[1][1] * a[1][1] + a[2][2] * a[2][2];
		if (off <= diag * 1e-30)
			break;
		for (const auto [p, q] : { std::pair(0, 1), std::pair(0, 2), std::pair(1, 2) })
		{
			if (std::abs(a[p][q]) < 1e-300)
				continue;
			const double theta = (a[q][q] - a[p][p]) / (2.0 * a[p][q]);
			const double t = (theta >= 0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
			const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
			for (int k = 0; k < 3; k++)
			{
				const double kp = a[k][p], kq = a[k][q];
				a[k][p] = c * kp - s * kq;
				a[k][q] = s * kp + c * kq;
			}
			for (int k = 0; k < 3; k++)
			{
				const double pk = a[p][k], qk = a[q][k];
				a[p][k] = c * pk - s * qk;
				a[q][k] = s * pk + c * qk;
			}
			for (int k = 0; k < 3; k++)
			{
				const double kp = v[k][p], kq = v[k][q];
				v[k][p] = c * kp - s * kq;
				v[k][q] = s * kp + c * kq;
			}
		}
	}
	int order[3] { 0, 1, 2 };
	std::sort(std::begin(order), std::end(order), [&](int i, int j) { return a[i][i] > a[j][j]; });
	for (int r = 0; r < 3; r++)
		axes[r] = glm::normalize(dvec3(v[0][order[r]], v[1][order[r]], v[2][order[r]]));
}

// Rows x, y, z of gun space in eye space: z from the muzzle back along the barrel, y up
// (away from the grip).
void gunAxes(const Tris& gun, dvec3 out[3])
{
	const std::vector<dvec3> pts = corners(gun);
	const dvec3 mid = mean(pts);
	dvec3 ax[3];
	principalAxes(pts, mid, ax);
	// the barrel: the direction of the longest edges (the slide's)
	std::vector<dvec3> edges;
	double longest = 0.0;
	for (const Tri& t : gun)
		for (const auto [i, j] : { std::pair(0, 1), std::pair(1, 2), std::pair(2, 0) })
		{
			const dvec3 d = t.p[j] - t.p[i];
			edges.push_back(glm::dot(d, ax[0]) >= 0 ? d : -d);
			longest = std::max(longest, glm::length(d));
		}
	dvec3 z(0.0);
	for (const dvec3& e : edges)
		if (glm::length(e) > 0.7 * longest)
			z += e;
	z = glm::normalize(z);
	// down: the grip, the far side of the second axis
	dvec3 y = glm::normalize(ax[1] - glm::dot(ax[1], z) * z);
	double lo = 1e300, hi = -1e300;
	for (const dvec3& p : pts)
	{
		const double s = glm::dot(p - mid, y);
		lo = std::min(lo, s);
		hi = std::max(hi, s);
	}
	if (hi > -lo)
	{
		y = -y;
		std::swap(lo, hi);
		lo = -lo;
		hi = -hi;
	}
	// back: where the bottom of the grip is
	double along = 0.0;
	size_t low = 0;
	for (const dvec3& p : pts)
		if (glm::dot(p - mid, y) < lo * 0.8)
		{
			along += glm::dot(p - mid, z);
			low++;
		}
	if (low > 0 && along / low < 0)
		z = -z;
	out[0] = glm::cross(y, z);
	out[1] = y;
	out[2] = z;
}

double median(std::vector<double> values)
{
	std::sort(values.begin(), values.end());
	const size_t n = values.size();
	if (n == 0)
		return 0.0;
	return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) * 0.5;
}

// Rows x, y, z of hand space in eye space (fingers -z, palm +x, thumb +y, for a left hand).
void openHandAxes(const Tris& hand, const Tris& cuff, dvec3 out[3])
{
	const std::vector<dvec3> hp = corners(hand), cp = corners(cuff);
	const dvec3 hc = mean(hp);
	std::vector<double> d;
	for (const dvec3& p : cp)
		d.push_back(glm::length(p - hc));
	const double md = median(d);
	std::vector<dvec3> ring;
	for (size_t i = 0; i < cp.size(); i++)
		if (d[i] <= md)
			ring.push_back(cp[i]);
	const dvec3 wrist = mean(ring);		// the cuff's hand-side ring
	const dvec3 f = glm::normalize(hc - wrist);
	dvec3 ax[3];
	principalAxes(hp, hc, ax);
	const dvec3 n = glm::normalize(ax[2] - glm::dot(ax[2], f) * f);
	// the fingers curl towards the palm: their tips are on the palm side of their middles
	double length = -1e300;
	for (const dvec3& p : hp)
		length = std::max(length, glm::dot(p - wrist, f));
	double tips = 0.0, middles = 0.0;
	size_t nt = 0, nm = 0;
	for (const dvec3& p : hp)
	{
		const double along = glm::dot(p - wrist, f);
		if (along > 0.9 * length)
		{
			tips += glm::dot(p, n);
			nt++;
		}
		if (along > 0.55 * length && along < 0.75 * length)
		{
			middles += glm::dot(p, n);
			nm++;
		}
	}
	const dvec3 palm = (nt ? tips / nt : 0.0) > (nm ? middles / nm : 0.0) ? n : -n;
	out[0] = palm;
	out[1] = glm::cross(palm, f);	// left hand: thumb = palm x fingers
	out[2] = -f;
}

Tris toSpace(const Tris& tris, const dvec3& origin, const dvec3 axes[3], double scale)
{
	Tris out = tris;
	for (Tri& t : out)
		for (dvec3& p : t.p)
		{
			const dvec3 d = p - origin;
			p = dvec3(glm::dot(axes[0], d), glm::dot(axes[1], d), glm::dot(axes[2], d)) * scale;
		}
	return out;
}

// Splits triangles at the plane at height y: below, above.
void clip(const Tris& tris, double y, Tris& below, Tris& above)
{
	for (const Tri& t : tris)
	{
		const double d[3] { t.p[0].y - y, t.p[1].y - y, t.p[2].y - y };
		if (d[0] >= -1e-6 && d[1] >= -1e-6 && d[2] >= -1e-6)
		{
			above.push_back(t);
			continue;
		}
		if (d[0] <= 1e-6 && d[1] <= 1e-6 && d[2] <= 1e-6)
		{
			below.push_back(t);
			continue;
		}
		for (int side = 0; side < 2; side++)
		{
			Tris& out = side == 0 ? below : above;
			std::vector<std::pair<dvec3, dvec2>> poly;
			for (int i = 0; i < 3; i++)
			{
				const int j = (i + 1) % 3;
				if (side == 0 ? d[i] <= 0 : d[i] >= 0)
					poly.emplace_back(t.p[i], t.uv[i]);
				if ((d[i] < 0) != (d[j] < 0) && d[i] != 0 && d[j] != 0)
				{
					const double s = d[i] / (d[i] - d[j]);
					poly.emplace_back(t.p[i] + s * (t.p[j] - t.p[i]), t.uv[i] + s * (t.uv[j] - t.uv[i]));
				}
			}
			for (size_t k = 1; k + 1 < poly.size(); k++)
			{
				Tri n = t;
				const size_t idx[3] { 0, k, k + 1 };
				for (int c = 0; c < 3; c++)
				{
					n.p[c] = poly[idx[c]].first;
					n.uv[c] = poly[idx[c]].second;
				}
				if (glm::length(glm::cross(n.p[1] - n.p[0], n.p[2] - n.p[0])) > 1e-12)
					out.push_back(n);
			}
		}
	}
}

enum Face { Top = 1, Bottom = 2, Front = 4, Back = 8, Left = 16, Right = 32, AllFaces = 63 };

// An untextured box, outward wound.
void box(Tris& out, const dvec3& lo, const dvec3& hi, const uint8_t col[4], int faces)
{
	const double x0 = lo.x, y0 = lo.y, z0 = lo.z, x1 = hi.x, y1 = hi.y, z1 = hi.z;
	const struct { Face face; dvec3 q[4]; } quads[] {
		{ Top, { { x0, y1, z0 }, { x0, y1, z1 }, { x1, y1, z1 }, { x1, y1, z0 } } },
		{ Bottom, { { x0, y0, z0 }, { x1, y0, z0 }, { x1, y0, z1 }, { x0, y0, z1 } } },
		{ Front, { { x0, y0, z0 }, { x0, y1, z0 }, { x1, y1, z0 }, { x1, y0, z0 } } },
		{ Back, { { x0, y0, z1 }, { x1, y0, z1 }, { x1, y1, z1 }, { x0, y1, z1 } } },
		{ Left, { { x0, y0, z0 }, { x0, y0, z1 }, { x0, y1, z1 }, { x0, y1, z0 } } },
		{ Right, { { x1, y0, z0 }, { x1, y1, z0 }, { x1, y1, z1 }, { x1, y0, z1 } } },
	};
	for (const auto& quad : quads)
	{
		if (!(faces & quad.face))
			continue;
		for (const auto [a, b, c] : { std::tuple(0, 1, 2), std::tuple(0, 2, 3) })
		{
			Tri t;
			t.p[0] = quad.q[a];
			t.p[1] = quad.q[b];
			t.p[2] = quad.q[c];
			t.uv[0] = t.uv[1] = t.uv[2] = dvec2(0.0);
			memcpy(t.col, col, 4);
			out.push_back(t);
		}
	}
}

// Per corner normals: the faces around a point within `crease` degrees of each other.
std::vector<std::array<dvec3, 3>> smoothNormals(const Tris& tris, double crease = 50.0)
{
	std::vector<dvec3> fn;
	fn.reserve(tris.size());
	for (const Tri& t : tris)
		fn.push_back(glm::normalize(glm::cross(t.p[1] - t.p[0], t.p[2] - t.p[0])));
	std::unordered_map<PointKey, std::vector<size_t>, PointHash> around;
	for (size_t i = 0; i < tris.size(); i++)
		for (const dvec3& p : tris[i].p)
			around[pointKey(p, 1e5)].push_back(i);
	const double cosCrease = std::cos(crease * 3.14159265358979323846 / 180.0);
	std::vector<std::array<dvec3, 3>> normals(tris.size());
	for (size_t i = 0; i < tris.size(); i++)
		for (int c = 0; c < 3; c++)
		{
			dvec3 n(0.0);
			for (size_t j : around[pointKey(tris[i].p[c], 1e5)])
				if (glm::dot(fn[j], fn[i]) >= cosCrease)
					n += fn[j];
			normals[i][c] = glm::normalize(n);
		}
	return normals;
}

void bounds(const Tris& tris, dvec3& lo, dvec3& hi)
{
	lo = dvec3(1e300);
	hi = dvec3(-1e300);
	for (const Tri& t : tris)
		for (const dvec3& p : t.p)
		{
			lo = glm::min(lo, p);
			hi = glm::max(hi, p);
		}
}

template<typename T>
void put(std::vector<uint8_t>& out, const T& value)
{
	const uint8_t *b = reinterpret_cast<const uint8_t *>(&value);
	out.insert(out.end(), b, b + sizeof(T));
}

constexpr uint32_t VqCodebook = 256 * 8;
constexpr uint32_t VqMip[] { 0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556 };
constexpr uint32_t OtherMip[] { 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558 };

// Index of pixel x, y in a twiddled w x h texture (as Flycast's twiddle_slow).
uint32_t twiddle(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	uint32_t rv = 0, sh = 0;
	uint32_t xs = w >> 1, ys = h >> 1;
	while (xs || ys)
	{
		if (ys)
		{
			rv |= (y & 1) << sh;
			y >>= 1;
			ys >>= 1;
			sh++;
		}
		if (xs)
		{
			rv |= (x & 1) << sh;
			x >>= 1;
			xs >>= 1;
			sh++;
		}
	}
	return rv;
}

void unpack16(uint32_t c, uint32_t fmt, uint8_t *rgba)
{
	switch (fmt)
	{
	case 0:	// ARGB1555
		rgba[0] = ((c >> 10) & 31) * 255 / 31;
		rgba[1] = ((c >> 5) & 31) * 255 / 31;
		rgba[2] = (c & 31) * 255 / 31;
		rgba[3] = c & 0x8000 ? 255 : 0;
		break;
	case 1:	// RGB565
		rgba[0] = ((c >> 11) & 31) * 255 / 31;
		rgba[1] = ((c >> 5) & 63) * 255 / 63;
		rgba[2] = (c & 31) * 255 / 31;
		rgba[3] = 255;
		break;
	default:	// ARGB4444
		rgba[0] = ((c >> 8) & 15) * 17;
		rgba[1] = ((c >> 4) & 15) * 17;
		rgba[2] = (c & 15) * 17;
		rgba[3] = ((c >> 12) & 15) * 17;
		break;
	}
}

}	// namespace

bool decodeTexture(const uint8_t *vram, size_t vramSize, const uint32_t *palette, uint32_t palCtrl,
		uint32_t tcw, uint32_t tsp, Image& image)
{
	const uint32_t w = 8u << ((tsp >> 3) & 7);
	uint32_t h = 8u << (tsp & 7);
	const uint32_t addr = (tcw & 0x1FFFFF) << 3;
	const bool planar = (tcw >> 26) & 1;
	uint32_t fmt = (tcw >> 27) & 7;
	const bool vq = (tcw >> 30) & 1;
	const bool mip = (tcw >> 31) & 1;
	const uint32_t palsel = (tcw >> 21) & 63;
	if (fmt == 7)
		fmt = 0;
	if (mip)
		h = w;
	uint32_t lg = 0;
	while ((1u << (lg + 1)) <= w)
		lg++;
	if (fmt == 3 || fmt == 4 || (vq && planar) || lg >= std::size(VqMip))
		return false;
	image.width = w;
	image.height = h;
	image.rgba.assign((size_t)w * h * 4, 0);
	auto byte = [&](size_t at, bool& ok) -> uint32_t {
		if (at >= vramSize)
		{
			ok = false;
			return 0;
		}
		return vram[at];
	};
	bool ok = true;
	for (uint32_t y = 0; y < h && ok; y++)
		for (uint32_t x = 0; x < w && ok; x++)
		{
			uint8_t *px = &image.rgba[((size_t)y * w + x) * 4];
			if (fmt == 5 || fmt == 6)
			{
				// palettised (always twiddled)
				const uint32_t bpp = fmt == 5 ? 4 : 8;
				const size_t base = addr + (mip ? OtherMip[lg] * bpp / 8 : 0);
				const uint32_t t = twiddle(x, y, w, h);
				uint32_t idx;
				if (bpp == 4)
				{
					const uint32_t b = byte(base + (t >> 1), ok);
					idx = (t & 1 ? b >> 4 : b & 15) + (palsel << 4);
				}
				else
					idx = byte(base + t, ok) + ((palsel >> 4) << 8);
				const uint32_t c = palette[idx & 1023];
				if ((palCtrl & 3) == 3)
				{
					px[0] = (c >> 16) & 255;
					px[1] = (c >> 8) & 255;
					px[2] = c & 255;
					px[3] = c >> 24;
				}
				else
					unpack16(c & 0xFFFF, palCtrl & 3, px);
				continue;
			}
			uint32_t c;
			if (vq)
			{
				const size_t base = addr + VqCodebook + (mip ? VqMip[lg] : 0);
				const uint32_t code = byte(base + twiddle(x >> 1, y >> 1, w >> 1, h >> 1), ok);
				const size_t entry = addr + (code * 4 + (((x & 1) << 1) | (y & 1))) * 2;
				c = byte(entry, ok) | byte(entry + 1, ok) << 8;
			}
			else if (planar)	// (stride selection ignored)
			{
				const size_t at = addr + ((size_t)y * w + x) * 2;
				c = byte(at, ok) | byte(at + 1, ok) << 8;
			}
			else
			{
				const size_t at = addr + (mip ? OtherMip[lg] * 2 : 0) + (size_t)twiddle(x, y, w, h) * 2;
				c = byte(at, ok) | byte(at + 1, ok) << 8;
			}
			unpack16(c, fmt, px);
		}
	return ok;
}

bool build(const Rip& rip, const Parts& parts, const TextureSource& textures, std::vector<uint8_t>& out, std::string& error)
{
	// the parts, by their textures
	auto pick = [&](std::initializer_list<uint32_t> texs) {
		Polys polys;
		for (const RipPoly& p : rip.polys)
		{
			if (p.v.size() < 3 || std::find(texs.begin(), texs.end(), p.texture()) == texs.end())
				continue;
			bool good = true;
			for (const RipVertex& v : p.v)
				good = good && std::isfinite(v.z) && v.z > 0.f;
			if (good)
				polys.push_back(&p);
		}
		return polys;
	};
	const Polys gun = pick({ parts.gun });
	std::vector<Polys> hands = clusters(rip, pick({ parts.hands[0], parts.hands[1] }));
	if (gun.empty() || hands.size() < 2)
	{
		error = "no pistol and two hands here";
		return false;
	}
	const dvec3 gc = centre(rip, gun);
	std::stable_sort(hands.begin(), hands.end(), [&](const Polys& a, const Polys& b) {
		return glm::length(centre(rip, a) - gc) < glm::length(centre(rip, b) - gc);
	});
	const Polys& gunHand = hands[0];
	const Polys& openHand = hands[1];
	Polys gunCuff, openCuff;
	const dvec3 ghc = centre(rip, gunHand), ohc = centre(rip, openHand);
	for (const Polys& c : clusters(rip, pick({ parts.cuff })))
	{
		const dvec3 cc = centre(rip, c);
		Polys& to = glm::length(cc - ghc) < glm::length(cc - ohc) ? gunCuff : openCuff;
		to.insert(to.end(), c.begin(), c.end());
	}
	if (openCuff.empty())
	{
		error = "no cuff on the open hand";
		return false;
	}
	auto join = [](Polys a, const Polys& b) {
		a.insert(a.end(), b.begin(), b.end());
		return a;
	};
	const Tris gunT = triangles(rip, gun);
	const Tris gunHandT = triangles(rip, join(gunHand, gunCuff));
	const Tris openT = triangles(rip, join(openHand, openCuff));

	// gun space: upright along the barrel, origin in the fist around the grip
	dvec3 axes[3];
	gunAxes(gunT, axes);
	const dvec3 fist = surfaceCentre(triangles(rip, gunHand));
	const Tris gunM = toSpace(gunT, fist, axes, Metres);
	Tris handM = toSpace(gunHandT, fist, axes, Metres);
	dvec3 lo, hi;
	bounds(gunM, lo, hi);
	const double length = hi.z - lo.z;
	// the muzzle: the middle of the front-most face (the slide's nose)
	dvec3 flo(1e300), fhi(-1e300);
	for (const dvec3& p : corners(gunM))
		if (p.z < lo.z + 0.002)
		{
			flo = glm::min(flo, p);
			fhi = glm::max(fhi, p);
		}
	const dvec3 muzzle((flo.x + fhi.x) / 2, (flo.y + fhi.y) / 2, lo.z);
	// the slide: above the bottom of its nose, where the line on its sides is
	const double cut = flo.y;
	Tris frameM, slideM;
	clip(gunM, cut, frameM, slideM);
	if (slideM.empty() || frameM.empty())
	{
		error = "the slide didn't come off";
		return false;
	}
	dvec3 sLo, sHi;
	bounds(slideM, sLo, sHi);
	double bodyFront = 1e300;		// behind the nose
	for (const dvec3& p : corners(slideM))
		if (p.z > lo.z + 0.002)
			bodyFront = std::min(bodyFront, p.z);
	const uint8_t dark[4] { 70, 70, 74, 255 }, darker[4] { 38, 38, 42, 255 };
	const double gap = 0.0004;
	const double inner = (sHi.x - sLo.x) * 0.03;
	// under the slide and on top of the frame: closed off, seen once the slide moves
	box(slideM, dvec3(sLo.x + inner, cut + gap, bodyFront), dvec3(sHi.x - inner, cut + gap, sHi.z), dark, Bottom);
	box(frameM, dvec3(sLo.x + inner, cut - gap, bodyFront), dvec3(sHi.x - inner, cut - gap, sHi.z - 0.15 * length), dark, Top);
	// the barrel, which stays when the slide goes back
	const double r = (sHi.x - sLo.x) * 0.22;
	box(frameM, dvec3(muzzle.x - r, muzzle.y - r, lo.z + 0.001), dvec3(muzzle.x + r, muzzle.y + r, lo.z + 0.35 * length), darker, AllFaces);
	const double travel = SlideTravel * length;
	const dvec3 grab(muzzle.x, (cut + sHi.y) / 2, sHi.z - 0.12 * length);

	// hand space: the open left hand, origin just off its palm
	dvec3 hAxes[3];
	openHandAxes(triangles(rip, openHand), triangles(rip, openCuff), hAxes);
	const dvec3 palm = surfaceCentre(triangles(rip, openHand)) + hAxes[0] * PalmOffset;
	const Tris openM = toSpace(openT, palm, hAxes, Metres);

	// the open hand on the slide (gun space, slide at rest): palm down on its rear, fingers
	// over to the right side, thumb back towards the shooter. Columns: hand x, y, z in gun space.
	float onSlide[16] {};
	const double rows[3][3] { { 0, -1, 0 }, { 0, 0, 1 }, { -1, 0, 0 } };
	for (int c = 0; c < 3; c++)
		for (int rr = 0; rr < 3; rr++)
			onSlide[c * 4 + rr] = (float)rows[c][rr];
	onSlide[12] = (float)(grab.x + GrabShift[0] * length);
	onSlide[13] = (float)(sHi.y + GrabShift[1] * length - PalmOffset * Metres);
	onSlide[14] = (float)grab.z;
	onSlide[15] = 1.f;

	// textures: in the order they're first used, index 0 plain white (untextured parts)
	const Tris *meshes[] { &frameM, &slideM, &handM, &openM };
	std::vector<Key> keys;
	for (const Tris *mesh : meshes)
		for (const Tri& t : *mesh)
			if (!t.tex.none && std::find(keys.begin(), keys.end(), t.tex) == keys.end())
				keys.push_back(t.tex);
	std::vector<Image> images(keys.size());
	for (size_t i = 0; i < keys.size(); i++)
		if (!textures(keys[i].tcw, keys[i].size, images[i]) || images[i].width == 0)
		{
			error = "a texture couldn't be read";
			return false;
		}

	out.clear();
	out.insert(out.end(), { 'H', 'N', 'D', '1' });
	put(out, (uint32_t)1);
	for (const dvec3& v : { muzzle, grab })
		for (int k = 0; k < 3; k++)
			put(out, (float)v[k]);
	put(out, (float)travel);
	for (float f : onSlide)
		put(out, f);
	put(out, (uint32_t)(keys.size() + 1));
	put(out, (uint32_t)1);
	put(out, (uint32_t)1);
	put(out, (uint32_t)0);
	out.insert(out.end(), { 255, 255, 255, 255 });
	for (size_t i = 0; i < keys.size(); i++)
	{
		Image& img = images[i];
		if (!(keys[i].flags & 16))
			for (size_t p = 3; p < img.rgba.size(); p += 4)
				img.rgba[p] = 255;
		put(out, img.width);
		put(out, img.height);
		put(out, keys[i].flags);
		out.insert(out.end(), img.rgba.begin(), img.rgba.end());
	}
	put(out, (uint32_t)std::size(meshes));
	for (const Tris *mesh : meshes)
	{
		const auto normals = smoothNormals(*mesh);
		std::map<uint32_t, std::vector<size_t>> groups;
		for (size_t i = 0; i < mesh->size(); i++)
		{
			const Key& k = (*mesh)[i].tex;
			groups[k.none ? 0 : (uint32_t)(std::find(keys.begin(), keys.end(), k) - keys.begin()) + 1].push_back(i);
		}
		put(out, (uint32_t)groups.size());
		for (const auto& [tex, items] : groups)
		{
			put(out, tex);
			put(out, (uint32_t)(items.size() * 3));
			for (size_t i : items)
			{
				const Tri& t = (*mesh)[i];
				for (int c = 0; c < 3; c++)
				{
					for (int k = 0; k < 3; k++)
						put(out, (float)t.p[c][k]);
					for (int k = 0; k < 3; k++)
						put(out, (float)normals[i][c][k]);
					put(out, (float)t.uv[c].x);
					put(out, (float)t.uv[c].y);
					out.insert(out.end(), std::begin(t.col), std::end(t.col));
				}
			}
		}
	}
	return true;
}

}
