//////////////////////////////////////////////////////////////////////////////////////////
//
//  Copyright (c) 2024 Advanced Micro Devices, Inc. All Rights Reserved.
//
//  Permission is hereby granted, free of charge, to any person obtaining a copy
//  of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
//  copies of the Software, and to permit persons to whom the Software is
//  furnished to do so, subject to the following conditions:
//
//  The above copyright notice and this permission notice shall be included in all
//  copies or substantial portions of the Software.
//
//  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
//  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
//  SOFTWARE.
//
//////////////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <hiprt/hiprt_types.h>
#include <hiprt/impl/Aabb.h>
#include <hiprt/impl/QrDecomposition.h>

namespace hiprt
{
struct SRTFrame;
struct MatrixFrame;

struct alignas( 64 ) Frame
{
	HIPRT_HOST_DEVICE Frame() : m_time( 0.0f )
	{
		m_scale		  = make_float3( 1.0f );
		m_shear		  = make_float3( 0.0f );
		m_translation = make_float3( 0.0f );
		m_rotation	  = { 0.0f, 0.0f, 0.0f, 1.0f };
	}

	HIPRT_HOST_DEVICE float3 transform( const float3& p ) const
	{
		if ( identity() ) return p;
		float3 result = p;
		result *= m_scale;
		result += float3{ p.y * m_shear.x + p.z * m_shear.y, p.z * m_shear.z, 0.0f };
		result = qtRotate( m_rotation, result );
		result += m_translation;
		return result;
	}

	HIPRT_HOST_DEVICE float3 transformVector( const float3& v ) const
	{
		if ( identity() ) return v;
		float3 result = v;
		result /= m_scale;
		result.y -= v.x * m_shear.x / m_scale.y;
		result.z -= ( m_shear.y * result.x + m_shear.z * result.y ) / m_scale.z;
		result = qtRotate( m_rotation, result );
		return result;
	}

	HIPRT_HOST_DEVICE float3 invTransform( const float3& p ) const
	{
		if ( identity() ) return p;
		float3 result = p;
		result -= m_translation;
		result = qtInvRotate( m_rotation, result );
		result /= m_scale;
		result.y -= p.z * m_shear.z / m_scale.y;
		result.x -= ( m_shear.x * result.y + m_shear.y * result.z ) / m_scale.x;
		return result;
	}

	HIPRT_HOST_DEVICE float3 invTransformVector( const float3& v ) const
	{
		if ( identity() ) return v;
		float3 result = v;
		result		  = qtInvRotate( m_rotation, result );
		result *= m_scale;
		result += float3{ 0.0f, v.x * m_shear.x, v.x * m_shear.y + v.y * m_shear.z };
		return result;
	}

	HIPRT_HOST_DEVICE bool identity() const
	{
		if ( m_scale.x != 1.0f || m_scale.y != 1.0f || m_scale.z != 1.0f ) return false;
		if ( m_shear.x != 0.0f || m_shear.y != 0.0f || m_shear.z != 0.0f ) return false;
		if ( m_translation.x != 0.0f || m_translation.y != 0.0f || m_translation.z != 0.0f ) return false;
		if ( m_rotation.w != 1.0f ) return false;
		return true;
	}

	float4 m_rotation;
	float3 m_scale;
	float3 m_shear;
	float3 m_translation;
	float  m_time;
};
HIPRT_STATIC_ASSERT( sizeof( Frame ) == 64 );

struct alignas( 16 ) SRTFrame
{
	float4 m_rotation;
	float3 m_scale;
	float3 m_translation;
	float  m_time;

	HIPRT_HOST_DEVICE Frame convert() const
	{
		Frame frame;
		frame.m_time		= m_time;
		frame.m_rotation	= qtFromAxisAngle( m_rotation );
		frame.m_scale		= m_scale;
		frame.m_shear		= make_float3( 0.0f );
		frame.m_translation = m_translation;
		return frame;
	}

	static HIPRT_HOST_DEVICE SRTFrame getSRTFrame( const Frame& frame )
	{
		SRTFrame srtFrame;
		srtFrame.m_time		   = frame.m_time;
		srtFrame.m_translation = frame.m_translation;
		srtFrame.m_scale	   = frame.m_scale;
		srtFrame.m_rotation	   = qtToAxisAngle( frame.m_rotation );
		return srtFrame;
	}

	static HIPRT_HOST_DEVICE SRTFrame getSRTFrameInv( const Frame& frame )
	{
		SRTFrame srtFrame;
		srtFrame.m_time		   = frame.m_time;
		srtFrame.m_translation = -frame.m_translation;
		srtFrame.m_scale	   = 1.0f / frame.m_scale;
		srtFrame.m_rotation	   = qtToAxisAngle( frame.m_rotation );
		srtFrame.m_rotation.w *= -1.0f;
		return srtFrame;
	}
};
HIPRT_STATIC_ASSERT( sizeof( SRTFrame ) == 48 );

struct alignas( 64 ) MatrixFrame
{
	float m_matrix[3][4];
	float m_time;

	HIPRT_HOST_DEVICE float3 transform( const float3& p ) const
	{
		if ( identity() ) return p;
		float3 result;
		result.x = m_matrix[0][0] * p.x + m_matrix[0][1] * p.y + m_matrix[0][2] * p.z + m_matrix[0][3];
		result.y = m_matrix[1][0] * p.x + m_matrix[1][1] * p.y + m_matrix[1][2] * p.z + m_matrix[1][3];
		result.z = m_matrix[2][0] * p.x + m_matrix[2][1] * p.y + m_matrix[2][2] * p.z + m_matrix[2][3];
		return result;
	}

	HIPRT_HOST_DEVICE float3 transformVector( const float3& v ) const
	{
		if ( identity() ) return v;
		// Transform as direction (ignore translation)
		float3 result;
		result.x = m_matrix[0][0] * v.x + m_matrix[0][1] * v.y + m_matrix[0][2] * v.z;
		result.y = m_matrix[1][0] * v.x + m_matrix[1][1] * v.y + m_matrix[1][2] * v.z;
		result.z = m_matrix[2][0] * v.x + m_matrix[2][1] * v.y + m_matrix[2][2] * v.z;
		return result;
	}

	HIPRT_HOST_DEVICE float3 invTransform( const float3& p ) const
	{
		if ( identity() ) return p;
		
		// Subtract translation first
		float3 pt;
		pt.x = p.x - m_matrix[0][3];
		pt.y = p.y - m_matrix[1][3];
		pt.z = p.z - m_matrix[2][3];
		
		// Compute inverse of 3x3 rotation/scale matrix
		float inv[3][3];
		if ( !computeInverse3x3( inv ) )
		{
			// Matrix is singular, return identity transformation
			return p;
		}
		
		// Apply inverse matrix to translated point
		float3 result;
		result.x = inv[0][0] * pt.x + inv[0][1] * pt.y + inv[0][2] * pt.z;
		result.y = inv[1][0] * pt.x + inv[1][1] * pt.y + inv[1][2] * pt.z;
		result.z = inv[2][0] * pt.x + inv[2][1] * pt.y + inv[2][2] * pt.z;
		return result;
	}

	HIPRT_HOST_DEVICE float3 invTransformVector( const float3& v ) const
	{
		if ( identity() ) return v;
		
		// For vectors, only apply inverse of 3x3 rotation/scale matrix (no translation)
		float inv[3][3];
		if ( !computeInverse3x3( inv ) )
		{
			// Matrix is singular, return identity transformation
			return v;
		}
		
		// Apply inverse matrix to vector
		float3 result;
		result.x = inv[0][0] * v.x + inv[0][1] * v.y + inv[0][2] * v.z;
		result.y = inv[1][0] * v.x + inv[1][1] * v.y + inv[1][2] * v.z;
		result.z = inv[2][0] * v.x + inv[2][1] * v.y + inv[2][2] * v.z;
		return result;
	}

	HIPRT_HOST_DEVICE bool identity() const
	{
		// Check if matrix is identity
		if ( m_matrix[0][0] != 1.0f || m_matrix[1][1] != 1.0f || m_matrix[2][2] != 1.0f ) return false;
		if ( m_matrix[0][1] != 0.0f || m_matrix[0][2] != 0.0f || m_matrix[0][3] != 0.0f ) return false;
		if ( m_matrix[1][0] != 0.0f || m_matrix[1][2] != 0.0f || m_matrix[1][3] != 0.0f ) return false;
		if ( m_matrix[2][0] != 0.0f || m_matrix[2][1] != 0.0f || m_matrix[2][3] != 0.0f ) return false;
		return true;
	}

  private:
	// Helper method to compute the inverse of the 3x3 rotation/scale submatrix
	// Returns false if matrix is singular (determinant too close to zero)
	HIPRT_HOST_DEVICE bool computeInverse3x3( float inv[3][3] ) const
	{
		float m00 = m_matrix[0][0], m01 = m_matrix[0][1], m02 = m_matrix[0][2];
		float m10 = m_matrix[1][0], m11 = m_matrix[1][1], m12 = m_matrix[1][2];
		float m20 = m_matrix[2][0], m21 = m_matrix[2][1], m22 = m_matrix[2][2];
		
		// Calculate determinant
		float det = m00 * (m11 * m22 - m12 * m21) -
					m01 * (m10 * m22 - m12 * m20) +
					m02 * (m10 * m21 - m11 * m20);
		
		// Check for singular matrix
		if ( abs( det ) < 1e-10f ) return false;
		
		float invDet = 1.0f / det;
		
		// Calculate inverse matrix elements (cofactor matrix transposed / det)
		inv[0][0] = (m11 * m22 - m12 * m21) * invDet;
		inv[0][1] = (m02 * m21 - m01 * m22) * invDet;
		inv[0][2] = (m01 * m12 - m02 * m11) * invDet;
		inv[1][0] = (m12 * m20 - m10 * m22) * invDet;
		inv[1][1] = (m00 * m22 - m02 * m20) * invDet;
		inv[1][2] = (m02 * m10 - m00 * m12) * invDet;
		inv[2][0] = (m10 * m21 - m11 * m20) * invDet;
		inv[2][1] = (m01 * m20 - m00 * m21) * invDet;
		inv[2][2] = (m00 * m11 - m01 * m10) * invDet;
		
		return true;
	}

  public:

	HIPRT_HOST_DEVICE Frame convert() const
	{
		float QR[3][3], Q[3][3], R[3][3];
#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 3; ++j )
				QR[i][j] = m_matrix[i][j];
		qr( &QR[0][0], &Q[0][0], &R[0][0] );

		Frame frame;
		frame.m_time		= m_time;
		frame.m_translation = { m_matrix[0][3], m_matrix[1][3], m_matrix[2][3] };
		frame.m_rotation	= qtFromRotationMatrix( Q );
		frame.m_scale		= { R[0][0], R[1][1], R[2][2] };
		frame.m_shear		= { R[0][1], R[0][2], R[1][2] };
		return frame;
	}

	static HIPRT_HOST_DEVICE MatrixFrame getMatrixFrame( const Frame& frame )
	{
		MatrixFrame matrixFrame{};
		matrixFrame.m_time = frame.m_time;

		if ( frame.identity() )
		{
			matrixFrame.m_matrix[0][0] = 1.0f;
			matrixFrame.m_matrix[1][1] = 1.0f;
			matrixFrame.m_matrix[2][2] = 1.0f;
			return matrixFrame;
		}

		float Q[3][3];
		qtToRotationMatrix( frame.m_rotation, Q );

		float R[3][3];
		R[0][0] = frame.m_scale.x;
		R[1][1] = frame.m_scale.y;
		R[2][2] = frame.m_scale.z;
		R[0][1] = frame.m_shear.x;
		R[0][2] = frame.m_shear.y;
		R[1][2] = frame.m_shear.z;
		R[1][0] = 0.0f;
		R[2][0] = 0.0f;
		R[2][1] = 0.0f;

#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 3; ++j )
#ifdef __KERNECC__
#pragma unroll
#endif
				for ( uint32_t k = 0; k < 3; ++k )
					matrixFrame.m_matrix[i][j] += Q[i][k] * R[k][j];

		matrixFrame.m_matrix[0][3] = frame.m_translation.x;
		matrixFrame.m_matrix[1][3] = frame.m_translation.y;
		matrixFrame.m_matrix[2][3] = frame.m_translation.z;

		return matrixFrame;
	}

	static HIPRT_HOST_DEVICE MatrixFrame getMatrixFrameInv( const Frame& frame )
	{
		MatrixFrame matrixFrame{};
		matrixFrame.m_time = frame.m_time;

		if ( frame.identity() )
		{
			matrixFrame.m_matrix[0][0] = 1.0f;
			matrixFrame.m_matrix[1][1] = 1.0f;
			matrixFrame.m_matrix[2][2] = 1.0f;
			return matrixFrame;
		}

		float Q[3][3];
		qtToRotationMatrix( frame.m_rotation, Q );

		float Ri[3][3];
		Ri[0][0] = 1.0f / frame.m_scale.x;
		Ri[1][1] = 1.0f / frame.m_scale.y;
		Ri[2][2] = 1.0f / frame.m_scale.z;
		Ri[0][1] = -frame.m_shear.x / ( frame.m_scale.x * frame.m_scale.y );
		Ri[0][2] = ( frame.m_shear.x * frame.m_shear.z - frame.m_shear.y * frame.m_scale.y ) /
				   ( frame.m_scale.x * frame.m_scale.y * frame.m_scale.z );
		Ri[1][2] = -frame.m_shear.z / ( frame.m_scale.y * frame.m_scale.z );
		Ri[1][0] = 0.0f;
		Ri[2][0] = 0.0f;
		Ri[2][1] = 0.0f;

#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 3; ++j )
#ifdef __KERNECC__
#pragma unroll
#endif
				for ( uint32_t k = 0; k < 3; ++k )
					matrixFrame.m_matrix[i][j] += Ri[i][k] * Q[j][k];

		matrixFrame.m_matrix[0][3] =
			-( matrixFrame.m_matrix[0][0] * frame.m_translation.x + matrixFrame.m_matrix[0][1] * frame.m_translation.y +
			   matrixFrame.m_matrix[0][2] * frame.m_translation.z );
		matrixFrame.m_matrix[1][3] =
			-( matrixFrame.m_matrix[1][0] * frame.m_translation.x + matrixFrame.m_matrix[1][1] * frame.m_translation.y +
			   matrixFrame.m_matrix[1][2] * frame.m_translation.z );
		matrixFrame.m_matrix[2][3] =
			-( matrixFrame.m_matrix[2][0] * frame.m_translation.x + matrixFrame.m_matrix[2][1] * frame.m_translation.y +
			   matrixFrame.m_matrix[2][2] * frame.m_translation.z );

		return matrixFrame;
	}

	static HIPRT_HOST_DEVICE MatrixFrame getMatrixFrameInv( const MatrixFrame& matrixFrame )
	{
		MatrixFrame invMatrixFrame{};
		invMatrixFrame.m_time = matrixFrame.m_time;

		// Check if identity
		if ( matrixFrame.identity() )
		{
			invMatrixFrame.m_matrix[0][0] = 1.0f;
			invMatrixFrame.m_matrix[1][1] = 1.0f;
			invMatrixFrame.m_matrix[2][2] = 1.0f;
			return invMatrixFrame;
		}

		// Extract 3x3 rotation/scale part
		const float* m = &matrixFrame.m_matrix[0][0];
		
		// Compute determinant
		float det = m[0] * ( m[5] * m[10] - m[6] * m[9] ) -
					m[1] * ( m[4] * m[10] - m[6] * m[8] ) +
					m[2] * ( m[4] * m[9] - m[5] * m[8] );

		// Check for singular matrix
		if ( abs( det ) < 1e-10f )
		{
			invMatrixFrame.m_matrix[0][0] = 1.0f;
			invMatrixFrame.m_matrix[1][1] = 1.0f;
			invMatrixFrame.m_matrix[2][2] = 1.0f;
			return invMatrixFrame;
		}

		float invDet = 1.0f / det;

		// Compute inverse of 3x3 rotation/scale part using cofactor method
		invMatrixFrame.m_matrix[0][0] = ( m[5] * m[10] - m[6] * m[9] ) * invDet;
		invMatrixFrame.m_matrix[0][1] = ( m[2] * m[9] - m[1] * m[10] ) * invDet;
		invMatrixFrame.m_matrix[0][2] = ( m[1] * m[6] - m[2] * m[5] ) * invDet;
		
		invMatrixFrame.m_matrix[1][0] = ( m[6] * m[8] - m[4] * m[10] ) * invDet;
		invMatrixFrame.m_matrix[1][1] = ( m[0] * m[10] - m[2] * m[8] ) * invDet;
		invMatrixFrame.m_matrix[1][2] = ( m[2] * m[4] - m[0] * m[6] ) * invDet;
		
		invMatrixFrame.m_matrix[2][0] = ( m[4] * m[9] - m[5] * m[8] ) * invDet;
		invMatrixFrame.m_matrix[2][1] = ( m[1] * m[8] - m[0] * m[9] ) * invDet;
		invMatrixFrame.m_matrix[2][2] = ( m[0] * m[5] - m[1] * m[4] ) * invDet;

		// Compute inverse translation: -R^-1 * t
		invMatrixFrame.m_matrix[0][3] = -( invMatrixFrame.m_matrix[0][0] * m[3] + 
										   invMatrixFrame.m_matrix[0][1] * m[7] + 
										   invMatrixFrame.m_matrix[0][2] * m[11] );
		invMatrixFrame.m_matrix[1][3] = -( invMatrixFrame.m_matrix[1][0] * m[3] + 
										   invMatrixFrame.m_matrix[1][1] * m[7] + 
										   invMatrixFrame.m_matrix[1][2] * m[11] );
		invMatrixFrame.m_matrix[2][3] = -( invMatrixFrame.m_matrix[2][0] * m[3] + 
										   invMatrixFrame.m_matrix[2][1] * m[7] + 
										   invMatrixFrame.m_matrix[2][2] * m[11] );

		return invMatrixFrame;
	}

	static HIPRT_HOST_DEVICE MatrixFrame multiply( const MatrixFrame& matrix0, const MatrixFrame& matrix1 )
	{
		MatrixFrame matrix{};
#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
		{
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 4; ++j )
			{
#ifdef __KERNECC__
#pragma unroll
#endif
				for ( uint32_t k = 0; k < 4; ++k )
				{
					float m0 = matrix0.m_matrix[i][k];
					float m1 = j < 3 ? 0.0f : 1.0f;
					if ( k < 3 ) m1 = matrix1.m_matrix[k][j];
					matrix.m_matrix[i][j] += m0 * m1;
				}
			}
		}
		return matrix;
	}
};
HIPRT_STATIC_ASSERT( sizeof( MatrixFrame ) == 64 );

// Template Transform class supporting both Frame and MatrixFrame storage
template <typename FrameType>
class Transform
{
  public:
	HIPRT_HOST_DEVICE Transform( const FrameType* frameData, uint32_t frameIndex, uint32_t frameCount )
		: m_frameCount( frameCount ), m_frames( nullptr )
	{
		if ( frameData != nullptr ) m_frames = frameData + frameIndex;
	}

	HIPRT_HOST_DEVICE FrameType interpolateFrames( float time ) const;
	HIPRT_HOST_DEVICE hiprtRay transformRay( const hiprtRay& ray, float time ) const;
	HIPRT_HOST_DEVICE float3 transformNormal( const float3& normal, float time ) const;
	HIPRT_HOST_DEVICE Aabb boundPointMotion( const float3& p ) const;
	HIPRT_HOST_DEVICE Aabb motionBounds( const Aabb& aabb ) const;

  private:
	uint32_t		  m_frameCount;
	const FrameType* m_frames;
};

// Specialization for Frame (original implementation with quaternion interpolation)
template <>
class Transform<Frame>
{
  public:
	HIPRT_HOST_DEVICE Transform( const Frame* frameData, uint32_t frameIndex, uint32_t frameCount )
		: m_frameCount( frameCount ), m_frames( nullptr )
	{
		if ( frameData != nullptr ) m_frames = frameData + frameIndex;
	}

	HIPRT_HOST_DEVICE Frame interpolateFrames( float time ) const
	{
		if ( m_frameCount == 0 || m_frames == nullptr ) return Frame();

		Frame f0 = m_frames[0];
		if ( m_frameCount == 1 || time == 0.0f || time <= f0.m_time ) return f0;

		Frame f1 = m_frames[m_frameCount - 1];
		if ( time >= f1.m_time ) return f1;

		for ( uint32_t i = 1; i < m_frameCount; ++i )
		{
			f1 = m_frames[i];
			if ( time >= f0.m_time && time <= f1.m_time ) break;
			f0 = f1;
		}

		float t = ( time - f0.m_time ) / ( f1.m_time - f0.m_time );

		Frame f;
		f.m_scale		= mix( f0.m_scale, f1.m_scale, t );
		f.m_shear		= mix( f0.m_shear, f1.m_shear, t );
		f.m_translation = mix( f0.m_translation, f1.m_translation, t );
		f.m_rotation	= qtMix( f0.m_rotation, f1.m_rotation, t );

		return f;
	}

	HIPRT_HOST_DEVICE hiprtRay transformRay( const hiprtRay& ray, float time ) const
	{
		hiprtRay outRay;
		Frame	 frame = interpolateFrames( time );
		if ( frame.identity() ) return ray;
		outRay.origin	 = frame.invTransform( ray.origin );
		outRay.direction = frame.invTransform( ray.origin + ray.direction );
		outRay.direction = outRay.direction - outRay.origin;
		outRay.minT		 = ray.minT;
		outRay.maxT		 = ray.maxT;
		return outRay;
	}

	HIPRT_HOST_DEVICE float3 transformNormal( const float3& normal, float time ) const
	{
		Frame frame = interpolateFrames( time );
		return frame.transformVector( normal );
	}

	HIPRT_HOST_DEVICE Aabb boundPointMotion( const float3& p ) const
	{
		Aabb outAabb;

		if ( m_frameCount == 0 || m_frames == nullptr )
		{
			outAabb.grow( p );
			return outAabb;
		}

		Frame f0 = m_frames[0];
		outAabb.grow( f0.transform( p ) );

		if ( m_frameCount == 1 ) return outAabb;

		constexpr uint32_t Steps = 3;
		constexpr float	   Delta = 1.0f / float( Steps + 1 );

		Frame f1;
		for ( uint32_t i = 1; i < m_frameCount; ++i )
		{
			f1		= m_frames[i];
			float t = Delta;
			for ( uint32_t j = 1; j <= Steps; ++j )
			{
				Frame f;
				f.m_scale		= mix( f0.m_scale, f1.m_scale, t );
				f.m_shear		= mix( f0.m_shear, f1.m_shear, t );
				f.m_translation = mix( f0.m_translation, f1.m_translation, t );
				f.m_rotation	= qtMix( f0.m_rotation, f1.m_rotation, t );
				outAabb.grow( f.transform( p ) );
				t += Delta;
			}
			f0 = f1;
			outAabb.grow( f0.transform( p ) );
		}

		return outAabb;
	}

	HIPRT_HOST_DEVICE Aabb motionBounds( const Aabb& aabb ) const
	{
		const float3 p0 = aabb.m_min;
		const float3 p1 = { aabb.m_min.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p2 = { aabb.m_min.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p3 = { aabb.m_min.x, aabb.m_max.y, aabb.m_max.z };
		const float3 p4 = { aabb.m_max.x, aabb.m_min.y, aabb.m_min.z };
		const float3 p5 = { aabb.m_max.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p6 = { aabb.m_max.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p7 = aabb.m_max;

		Aabb outAabb;
		outAabb.grow( boundPointMotion( p0 ) );
		outAabb.grow( boundPointMotion( p1 ) );
		outAabb.grow( boundPointMotion( p2 ) );
		outAabb.grow( boundPointMotion( p3 ) );
		outAabb.grow( boundPointMotion( p4 ) );
		outAabb.grow( boundPointMotion( p5 ) );
		outAabb.grow( boundPointMotion( p6 ) );
		outAabb.grow( boundPointMotion( p7 ) );
		return outAabb;
	}

  private:
	uint32_t	 m_frameCount;
	const Frame* m_frames;
};

// Specialization for SRTFrame (converts to Frame for transformation)
template <>
class Transform<SRTFrame>
{
  public:
	HIPRT_HOST_DEVICE Transform( const SRTFrame* frameData, uint32_t frameIndex, uint32_t frameCount )
		: m_frameCount( frameCount ), m_frames( nullptr )
	{
		if ( frameData != nullptr ) m_frames = frameData + frameIndex;
	}

	HIPRT_HOST_DEVICE Frame interpolateFrames( float time ) const
	{
		if ( m_frameCount == 0 || m_frames == nullptr ) return Frame();

		const SRTFrame& f0 = m_frames[0];
		if ( m_frameCount == 1 || time == 0.0f || time <= f0.m_time ) return f0.convert();

		const SRTFrame& f1 = m_frames[m_frameCount - 1];
		if ( time >= f1.m_time ) return f1.convert();

		// Find surrounding frames
		const SRTFrame* m0 = &m_frames[0];
		const SRTFrame* m1 = &m_frames[1];
		for ( uint32_t i = 1; i < m_frameCount; i++ )
		{
			m1 = &m_frames[i];
			if ( time <= m1->m_time )
			{
				m0 = &m_frames[i - 1];
				break;
			}
		}

		// Convert to Frame and interpolate
		Frame frame0 = m0->convert();
		Frame frame1 = m1->convert();

		// Interpolate between frames
		float t		 = ( time - m0->m_time ) / ( m1->m_time - m0->m_time );
		Frame result = frame0;
		result.m_time		 = time;
		result.m_scale		 = frame0.m_scale + ( frame1.m_scale - frame0.m_scale ) * t;
		result.m_shear		 = frame0.m_shear + ( frame1.m_shear - frame0.m_shear ) * t;
		result.m_translation = frame0.m_translation + ( frame1.m_translation - frame0.m_translation ) * t;
		result.m_rotation	 = qtMix( frame0.m_rotation, frame1.m_rotation, t );

		return result;
	}

	HIPRT_HOST_DEVICE hiprtRay transformRay( const hiprtRay& ray, float time ) const
	{
		Frame frame = interpolateFrames( time );
		return transformRayWithFrame( ray, frame );
	}

	HIPRT_HOST_DEVICE float3 transformNormal( const float3& normal, float time ) const
	{
		Frame frame = interpolateFrames( time );
		return frame.transformVector( normal );
	}

	HIPRT_HOST_DEVICE Aabb boundPointMotion( const float3& p ) const
	{
		Aabb outAabb;
		if ( m_frameCount == 0 || m_frames == nullptr )
		{
			outAabb.grow( p );
			return outAabb;
		}

		// Sample transformations at each keyframe
		for ( uint32_t i = 0; i < m_frameCount; i++ )
		{
			Frame frame = m_frames[i].convert();
			outAabb.grow( frame.transform( p ) );
		}

		return outAabb;
	}

	HIPRT_HOST_DEVICE Aabb motionBounds( const Aabb& aabb ) const
	{
		const float3 p0 = aabb.m_min;
		const float3 p1 = { aabb.m_min.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p2 = { aabb.m_min.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p3 = { aabb.m_min.x, aabb.m_max.y, aabb.m_max.z };
		const float3 p4 = { aabb.m_max.x, aabb.m_min.y, aabb.m_min.z };
		const float3 p5 = { aabb.m_max.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p6 = { aabb.m_max.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p7 = aabb.m_max;

		Aabb outAabb;
		outAabb.grow( boundPointMotion( p0 ) );
		outAabb.grow( boundPointMotion( p1 ) );
		outAabb.grow( boundPointMotion( p2 ) );
		outAabb.grow( boundPointMotion( p3 ) );
		outAabb.grow( boundPointMotion( p4 ) );
		outAabb.grow( boundPointMotion( p5 ) );
		outAabb.grow( boundPointMotion( p6 ) );
		outAabb.grow( boundPointMotion( p7 ) );
		return outAabb;
	}

  private:
	HIPRT_HOST_DEVICE hiprtRay transformRayWithFrame( const hiprtRay& ray, const Frame& frame ) const
	{
		// Check if frame is identity (optimization)
		if ( frame.identity() ) return ray;

		// Transform ray
		hiprtRay outRay;
		outRay.origin	 = frame.invTransform( ray.origin );
		outRay.direction = frame.invTransformVector( ray.direction );
		outRay.minT		 = ray.minT;
		outRay.maxT		 = ray.maxT;
		return outRay;
	}

	uint32_t		  m_frameCount;
	const SRTFrame* m_frames;
};

// Specialization for MatrixFrame (direct matrix interpolation, sacrifices slerp accuracy)
template <>
class Transform<MatrixFrame>
{
  public:
	HIPRT_HOST_DEVICE Transform( const MatrixFrame* frameData, uint32_t frameIndex, uint32_t frameCount )
		: m_frameCount( frameCount ), m_frames( nullptr )
	{
		if ( frameData != nullptr ) m_frames = frameData + frameIndex;
	}

	HIPRT_HOST_DEVICE MatrixFrame interpolateFrames( float time ) const
	{
		if ( m_frameCount == 0 || m_frames == nullptr ) return MatrixFrame();

		const MatrixFrame& f0 = m_frames[0];
		if ( m_frameCount == 1 || time == 0.0f || time <= f0.m_time ) return f0;

		const MatrixFrame& f1 = m_frames[m_frameCount - 1];
		if ( time >= f1.m_time ) return f1;

		// Find surrounding frames
		const MatrixFrame* m0 = &m_frames[0];
		const MatrixFrame* m1 = &m_frames[1];
		for ( uint32_t i = 1; i < m_frameCount; ++i )
		{
			m1 = &m_frames[i];
			if ( time >= m0->m_time && time <= m1->m_time ) break;
			m0 = m1;
		}

		// Linear interpolation of matrix elements (sacrifices rotation accuracy)
		float t = ( time - m0->m_time ) / ( m1->m_time - m0->m_time );

		MatrixFrame interpolated;
		interpolated.m_time = time;
#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 4; ++j )
				interpolated.m_matrix[i][j] = mix( m0->m_matrix[i][j], m1->m_matrix[i][j], t );

		return interpolated;
	}

	HIPRT_HOST_DEVICE hiprtRay transformRay( const hiprtRay& ray, float time ) const
	{
		if ( m_frameCount == 0 || m_frames == nullptr ) return ray;

		// Get interpolated matrix
		const MatrixFrame& f0 = m_frames[0];
		if ( m_frameCount == 1 || time == 0.0f || time <= f0.m_time )
		{
			return transformRayWithMatrix( ray, f0 );
		}

		const MatrixFrame& f1 = m_frames[m_frameCount - 1];
		if ( time >= f1.m_time )
		{
			return transformRayWithMatrix( ray, f1 );
		}

		// Find and interpolate
		const MatrixFrame* m0 = &m_frames[0];
		const MatrixFrame* m1 = &m_frames[1];
		for ( uint32_t i = 1; i < m_frameCount; ++i )
		{
			m1 = &m_frames[i];
			if ( time >= m0->m_time && time <= m1->m_time ) break;
			m0 = m1;
		}

		float t = ( time - m0->m_time ) / ( m1->m_time - m0->m_time );

		MatrixFrame interpolated;
#ifdef __KERNECC__
#pragma unroll
#endif
		for ( uint32_t i = 0; i < 3; ++i )
#ifdef __KERNECC__
#pragma unroll
#endif
			for ( uint32_t j = 0; j < 4; ++j )
				interpolated.m_matrix[i][j] = mix( m0->m_matrix[i][j], m1->m_matrix[i][j], t );

		return transformRayWithMatrix( ray, interpolated );
	}

	HIPRT_HOST_DEVICE float3 transformNormal( const float3& normal, float time ) const
	{
		MatrixFrame frame = interpolateFrames( time );
		return frame.transformVector( normal );
	}

	HIPRT_HOST_DEVICE Aabb boundPointMotion( const float3& p ) const
	{
		Aabb outAabb;

		if ( m_frameCount == 0 || m_frames == nullptr )
		{
			outAabb.grow( p );
			return outAabb;
		}

		// Use MatrixFrame directly without conversion
		outAabb.grow( m_frames[0].transform( p ) );

		if ( m_frameCount == 1 ) return outAabb;

		constexpr uint32_t Steps = 3;
		constexpr float	   Delta = 1.0f / float( Steps + 1 );

		for ( uint32_t i = 1; i < m_frameCount; ++i )
		{
			float t = Delta;
			for ( uint32_t j = 1; j <= Steps; ++j )
			{
				MatrixFrame interpolated;
#ifdef __KERNECC__
#pragma unroll
#endif
				for ( uint32_t r = 0; r < 3; ++r )
#ifdef __KERNECC__
#pragma unroll
#endif
					for ( uint32_t c = 0; c < 4; ++c )
						interpolated.m_matrix[r][c] = mix( m_frames[i - 1].m_matrix[r][c], m_frames[i].m_matrix[r][c], t );

				outAabb.grow( interpolated.transform( p ) );
				t += Delta;
			}
			outAabb.grow( m_frames[i].transform( p ) );
		}

		return outAabb;
	}

	HIPRT_HOST_DEVICE Aabb motionBounds( const Aabb& aabb ) const
	{
		const float3 p0 = aabb.m_min;
		const float3 p1 = { aabb.m_min.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p2 = { aabb.m_min.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p3 = { aabb.m_min.x, aabb.m_max.y, aabb.m_max.z };
		const float3 p4 = { aabb.m_max.x, aabb.m_min.y, aabb.m_min.z };
		const float3 p5 = { aabb.m_max.x, aabb.m_min.y, aabb.m_max.z };
		const float3 p6 = { aabb.m_max.x, aabb.m_max.y, aabb.m_min.z };
		const float3 p7 = aabb.m_max;

		Aabb outAabb;
		outAabb.grow( boundPointMotion( p0 ) );
		outAabb.grow( boundPointMotion( p1 ) );
		outAabb.grow( boundPointMotion( p2 ) );
		outAabb.grow( boundPointMotion( p3 ) );
		outAabb.grow( boundPointMotion( p4 ) );
		outAabb.grow( boundPointMotion( p5 ) );
		outAabb.grow( boundPointMotion( p6 ) );
		outAabb.grow( boundPointMotion( p7 ) );
		return outAabb;
	}

  private:
	HIPRT_HOST_DEVICE hiprtRay transformRayWithMatrix( const hiprtRay& ray, const MatrixFrame& mat ) const
	{
		// Check if matrix is identity (optimization)
		if ( mat.identity() ) return ray;

		// Compute inverse matrix transform for ray (world to local)
		hiprtRay outRay;
		outRay.origin	 = mat.invTransform( ray.origin );
		outRay.direction = mat.invTransform( ray.origin + ray.direction );
		outRay.direction = outRay.direction - outRay.origin;
		outRay.minT		 = ray.minT;
		outRay.maxT		 = ray.maxT;
		return outRay;
	}

	uint32_t			m_frameCount;
	const MatrixFrame* m_frames;
};
} // namespace hiprt
