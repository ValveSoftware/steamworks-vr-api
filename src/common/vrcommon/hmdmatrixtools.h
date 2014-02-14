//========= Copyright Valve Corporation ============//
#pragma once

#include "steamvr.h"
#include <math.h>

using vr::HmdMatrix34_t;
using vr::HmdMatrix44_t;

struct HmdVector3_t
{
	double v[3];
};

struct HmdQuaternion_t
{
	double w, x, y, z;
};

inline HmdVector3_t HmdVector_Init( double x, double y, double z )
{
	HmdVector3_t vec;
	vec.v[0] = x;
	vec.v[1] = y;
	vec.v[2] = z;
	return vec;
}

inline HmdMatrix34_t HmdQuaternion_ToRotationMatrix( const HmdQuaternion_t & quat )
{
	HmdMatrix34_t out;

	out.m[0][0] = (float)( 1.0 - 2.0*(quat.y*quat.y + quat.z*quat.z) );
	out.m[0][1] = (float)( 2.0*(quat.x*quat.y - quat.w*quat.z) );
	out.m[0][2] = (float)( 2.0*(quat.x*quat.z + quat.w*quat.y) );
	out.m[0][3] = 0;

	out.m[1][0] = (float)( 2.0*(quat.x*quat.y + quat.w*quat.z) );
	out.m[1][1] = (float)( 1.0 - 2.0*(quat.x*quat.x + quat.z*quat.z) );
	out.m[1][2] = (float)( 2.0*(quat.y*quat.z - quat.w*quat.x) );
	out.m[1][3] = 0;
	
	out.m[2][0] = (float)( 2.0*(quat.x*quat.z - quat.w*quat.y) );
	out.m[2][1] = (float)( 2.0*(quat.y*quat.z + quat.w*quat.x) );
	out.m[2][2] = (float)( 1.0 - 2.0*(quat.x*quat.x + quat.y*quat.y) );
	out.m[2][3] = 0;

	return out;
}

inline HmdQuaternion_t HmdQuaternion_Init( double w, double x, double y, double z )
{
	HmdQuaternion_t quat;
	quat.w = w;
	quat.x = x;
	quat.y = y;
	quat.z = z;
	return quat;
}

inline HmdQuaternion_t HmdQuaternion_Conjugate( const HmdQuaternion_t & q )
{
	return HmdQuaternion_Init( q.w, -q.x, -q.y, -q.z );
}

inline HmdQuaternion_t HmdQuaternion_Multiply( const HmdQuaternion_t & lhs, const HmdQuaternion_t & rhs )
{
	return HmdQuaternion_Init(
		lhs.w*rhs.w - (lhs.x*rhs.x + lhs.y*rhs.y + lhs.z*rhs.z),  // w*rhs.w - dot(xyz,rhs.xyz)
		lhs.w*rhs.x + lhs.x*rhs.w + (lhs.y*rhs.z - lhs.z*rhs.y),  // w*rhs.xyz + rhs.w*xyz + cross(xyz, rhs.xyz)
		lhs.w*rhs.y + lhs.y*rhs.w + (lhs.z*rhs.x - lhs.x*rhs.z),
		lhs.w*rhs.z + lhs.z*rhs.w + (lhs.x*rhs.y - lhs.y*rhs.x));
}

inline HmdVector3_t HmdQuaternion_RotateVector( const HmdQuaternion_t & q, const HmdVector3_t & v )
{
	// Equivalent to q * (0, v) * q.Conjugate().
	double uvx = 2.0 * (q.y*v.v[2] - q.z*v.v[1]);	// uv = 2  * cross(q.xyz, v)
	double uvy = 2.0 * (q.z*v.v[0] - q.x*v.v[2]);
	double uvz = 2.0 * (q.x*v.v[1] - q.y*v.v[0]);

	return HmdVector_Init(						// rotated = v + q.w * uv + cross(q.xyz, uv)
		v.v[0] + q.w*uvx + q.y*uvz - q.z*uvy,
		v.v[1] + q.w*uvy + q.z*uvx - q.x*uvz,
		v.v[2] + q.w*uvz + q.x*uvy - q.y*uvx );
}

inline double HmdVector_Dot( const HmdVector3_t & lhs, const HmdVector3_t & rhs )
{
	return lhs.v[0] * rhs.v[0] + lhs.v[1] * rhs.v[1] + lhs.v[2] * rhs.v[2];
}

inline HmdVector3_t HmdVector_Cross( const HmdVector3_t & lhs, const HmdVector3_t & rhs )
{
	HmdVector3_t vec;
	vec.v[0] = lhs.v[1]*rhs.v[2] - lhs.v[2]*rhs.v[1];
	vec.v[1] = lhs.v[2]*rhs.v[0] - lhs.v[0]*rhs.v[2];
	vec.v[2] = lhs.v[0]*rhs.v[1] - lhs.v[1]*rhs.v[0];
	return vec;
}

inline HmdVector3_t HmdVector_ScalarMultiply( const HmdVector3_t & vec, double scalar )
{
	HmdVector3_t out;
	out.v[0] = vec.v[0] * scalar;
	out.v[1] = vec.v[1] * scalar;
	out.v[2] = vec.v[2] * scalar;
	return out;
}

inline double HmdVector_Length( const HmdVector3_t & vec )
{
	return sqrt( HmdVector_Dot( vec, vec ) );

}

inline HmdVector3_t HmdVector_Normalized( const HmdVector3_t & vec )
{
	double len = HmdVector_Length( vec );
	if( len == 0 )
		return HmdVector_Init( 0, 0, 0 );
	else
		return HmdVector_Init( vec.v[0] / len, vec.v[1] / len, vec.v[2] / len );
}

inline bool HmdVector_Equals( const HmdVector3_t & lhs, const HmdVector3_t & rhs )
{
	return lhs.v[0] == rhs.v[0] 
		&& lhs.v[1] == rhs.v[1] 
		&& lhs.v[2] == rhs.v[2];
}

inline HmdVector3_t HmdVector_Subtract( const HmdVector3_t & lhs, const HmdVector3_t & rhs )
{
	return HmdVector_Init( lhs.v[0] - rhs.v[0], lhs.v[1] - rhs.v[1], lhs.v[2] - rhs.v[2] );
}

inline HmdQuaternion_t HmdQuaternion_FromAxisAngleVector( const HmdVector3_t & vec )
{
	double angle = HmdVector_Length( vec );
	if (angle == 0)
	{
		return HmdQuaternion_Init(1, 0, 0, 0);      // identity
	}
	else
	{
		double halfAngle = angle * 0.5; 
		double vectorScale = sin(halfAngle)/angle;
		return HmdQuaternion_Init( cos(halfAngle), vectorScale * vec.v[0], vectorScale * vec.v[1], vectorScale * vec.v[2] );
	}
}

inline HmdVector3_t HmdVector_Add( const HmdVector3_t & lhs, const HmdVector3_t & rhs )
{
	return HmdVector_Init( lhs.v[0] + rhs.v[0], lhs.v[1] + rhs.v[1], lhs.v[2] + rhs.v[2] );
}

inline HmdVector3_t HmdMatrix_GetBack( const HmdMatrix34_t & mat )
{
	return HmdVector_Init( mat.m[0][2], mat.m[1][2], mat.m[2][2] );
}

inline HmdVector3_t HmdMatrix_GetUp( const HmdMatrix34_t & mat )
{
	return HmdVector_Init( mat.m[0][1], mat.m[1][1], mat.m[2][1] );
}

inline HmdVector3_t HmdMatrix_GetRight( const HmdMatrix34_t & mat )
{
	return HmdVector_Init( mat.m[0][0], mat.m[1][0], mat.m[2][0] );
}

inline HmdVector3_t HmdMatrix_GetTranslation( const HmdMatrix34_t & mat )
{
	return HmdVector_Init( mat.m[0][3], mat.m[1][3], mat.m[2][3] );
}

inline void HmdMatrix_SetTranslationInline( HmdMatrix34_t *pMat, const HmdVector3_t & vec )
{
	pMat->m[0][3] = (float)vec.v[0];
	pMat->m[1][3] = (float)vec.v[1];
	pMat->m[2][3] = (float)vec.v[2];
}

inline HmdMatrix34_t HmdMatrix34_Init( const HmdVector3_t & right, const HmdVector3_t & up, const HmdVector3_t & back, const HmdVector3_t & translation )
{
	HmdMatrix34_t mat ={0};
	for( int i=0; i<3; i++ )
	{
		mat.m[i][0] = (float)right.v[i];
		mat.m[i][1] = (float)up.v[i];
		mat.m[i][2] = (float)back.v[i];
		mat.m[i][3] = (float)translation.v[i];
	}
	return mat;
}

inline void HmdMatrix_SetIdentity( HmdMatrix44_t *pMatrix )
{
	pMatrix->m[0][0] = 1.f;
	pMatrix->m[0][1] = 0.f;
	pMatrix->m[0][2] = 0.f;
	pMatrix->m[0][3] = 0.f;
	pMatrix->m[1][0] = 0.f;
	pMatrix->m[1][1] = 1.f;
	pMatrix->m[1][2] = 0.f;
	pMatrix->m[1][3] = 0.f;
	pMatrix->m[2][0] = 0.f;
	pMatrix->m[2][1] = 0.f;
	pMatrix->m[2][2] = 1.f;
	pMatrix->m[2][3] = 0.f;
	pMatrix->m[3][0] = 0.f;
	pMatrix->m[3][1] = 0.f;
	pMatrix->m[3][2] = 0.f;
	pMatrix->m[3][3] = 1.f;
}


inline void HmdMatrix_SetIdentity( HmdMatrix34_t *pMatrix )
{
	pMatrix->m[0][0] = 1.f;
	pMatrix->m[0][1] = 0.f;
	pMatrix->m[0][2] = 0.f;
	pMatrix->m[0][3] = 0.f;
	pMatrix->m[1][0] = 0.f;
	pMatrix->m[1][1] = 1.f;
	pMatrix->m[1][2] = 0.f;
	pMatrix->m[1][3] = 0.f;
	pMatrix->m[2][0] = 0.f;
	pMatrix->m[2][1] = 0.f;
	pMatrix->m[2][2] = 1.f;
	pMatrix->m[2][3] = 0.f;
}

inline HmdMatrix34_t HmdMatrix34_InitFromAxisAngleAndTranslation( const HmdVector3_t & vAxisAngleRotation, const HmdVector3_t & vTranslation )
{
	double angle = HmdVector_Length( vAxisAngleRotation );
	HmdVector3_t axis;
	if( angle == 0 )
		axis = vAxisAngleRotation;
	else
		axis = HmdVector_ScalarMultiply( vAxisAngleRotation, 1.0 / angle );

	float c = cosf( (float)angle );
	float s = sinf( (float)angle );
	float C = 1 - c;

	float x = (float)axis.v[0];
	float y = (float)axis.v[1];
	float z = (float)axis.v[2];

	HmdMatrix34_t mat;
	mat.m[0][0] = x * x * C + c;
	mat.m[1][0] = x * y * C - z * s;
	mat.m[2][0] = x * z * C + y * s;
	mat.m[0][1] = y * x * C + z * s;
	mat.m[1][1] = y * y * C + c;
	mat.m[2][1] = y * z * C - x * s;
	mat.m[0][2] = z * x * C - y * s;
	mat.m[1][2] = z * y * C + x * s;
	mat.m[2][2] = z * z * C + c;

	mat.m[0][3] = (float)vTranslation.v[0];
	mat.m[1][3] = (float)vTranslation.v[1];
	mat.m[2][3] = (float)vTranslation.v[2];

	return mat;
}

inline HmdMatrix34_t HmdMatrix_InvertTR( const HmdMatrix34_t & matrix )
{
	// Invert worldFromHead pose
	//
	// inverted.rotation = transpose(matrix.rotation)
	HmdMatrix34_t inverted = { 0 };
	for (int i = 0; i < 3; i++)
	{
		for (int j = 0; j < 3; j++)
			inverted.m[j][i] = matrix.m[i][j];
	}

	// inverted.position = inverted.rotation * -matrix.position
	for (int i = 0; i < 3; i++)
	{
		inverted.m[i][3] = 0;
		for (int j = 0; j < 3; j++)
			inverted.m[i][3] += inverted.m[i][j] * -matrix.m[j][3];
	}
	return inverted;
}

inline HmdMatrix44_t HmdMatrix_34to44( const HmdMatrix34_t & m34 )
{
	HmdMatrix44_t m44;
	for( int i=0; i<3; i++ )
		for( int j=0; j<4; j++ )
		{
			m44.m[i][j] = m34.m[i][j];
		}

	m44.m[3][0] = 0.f;
	m44.m[3][1] = 0.f;
	m44.m[3][2] = 0.f;
	m44.m[3][3] = 1.f;
	return m44;
}

inline HmdMatrix34_t HmdMatrix_44to34( const HmdMatrix44_t & m44 )
{
	HmdMatrix34_t m34;
	for( int i=0; i<3; i++ )
		for( int j=0; j<4; j++ )
		{
			m34.m[i][j] = m44.m[i][j];
		}

	return m34;
}



inline HmdMatrix44_t HmdMatrix_Multiply( const HmdMatrix44_t & left, const HmdMatrix44_t & right )
{
	HmdMatrix44_t out;	
	out.m[0][0] = 	left.m[0][0]*right.m[0][0] + left.m[0][1]*right.m[1][0] + left.m[0][2]*right.m[2][0] + left.m[0][3]*right.m[3][0];
	out.m[0][1] = 	left.m[0][0]*right.m[0][1] + left.m[0][1]*right.m[1][1] + left.m[0][2]*right.m[2][1] + left.m[0][3]*right.m[3][1];
	out.m[0][2] = 	left.m[0][0]*right.m[0][2] + left.m[0][1]*right.m[1][2] + left.m[0][2]*right.m[2][2] + left.m[0][3]*right.m[3][2];
	out.m[0][3] = 	left.m[0][0]*right.m[0][3] + left.m[0][1]*right.m[1][3] + left.m[0][2]*right.m[2][3] + left.m[0][3]*right.m[3][3];

	out.m[1][0] = 	left.m[1][0]*right.m[0][0] + left.m[1][1]*right.m[1][0] + left.m[1][2]*right.m[2][0] + left.m[1][3]*right.m[3][0];
	out.m[1][1] = 	left.m[1][0]*right.m[0][1] + left.m[1][1]*right.m[1][1] + left.m[1][2]*right.m[2][1] + left.m[1][3]*right.m[3][1];
	out.m[1][2] = 	left.m[1][0]*right.m[0][2] + left.m[1][1]*right.m[1][2] + left.m[1][2]*right.m[2][2] + left.m[1][3]*right.m[3][2];
	out.m[1][3] = 	left.m[1][0]*right.m[0][3] + left.m[1][1]*right.m[1][3] + left.m[1][2]*right.m[2][3] + left.m[1][3]*right.m[3][3];

	out.m[2][0] = 	left.m[2][0]*right.m[0][0] + left.m[2][1]*right.m[1][0] + left.m[2][2]*right.m[2][0] + left.m[2][3]*right.m[3][0];
	out.m[2][1] = 	left.m[2][0]*right.m[0][1] + left.m[2][1]*right.m[1][1] + left.m[2][2]*right.m[2][1] + left.m[2][3]*right.m[3][1];
	out.m[2][2] = 	left.m[2][0]*right.m[0][2] + left.m[2][1]*right.m[1][2] + left.m[2][2]*right.m[2][2] + left.m[2][3]*right.m[3][2];
	out.m[2][3] = 	left.m[2][0]*right.m[0][3] + left.m[2][1]*right.m[1][3] + left.m[2][2]*right.m[2][3] + left.m[2][3]*right.m[3][3];

	out.m[3][0] = 	left.m[3][0]*right.m[0][0] + left.m[3][1]*right.m[1][0] + left.m[3][2]*right.m[2][0] + left.m[3][3]*right.m[3][0];
	out.m[3][1] = 	left.m[3][0]*right.m[0][1] + left.m[3][1]*right.m[1][1] + left.m[3][2]*right.m[2][1] + left.m[3][3]*right.m[3][1];
	out.m[3][2] = 	left.m[3][0]*right.m[0][2] + left.m[3][1]*right.m[1][2] + left.m[3][2]*right.m[2][2] + left.m[3][3]*right.m[3][2];
	out.m[3][3] = 	left.m[3][0]*right.m[0][3] + left.m[3][1]*right.m[1][3] + left.m[3][2]*right.m[2][3] + left.m[3][3]*right.m[3][3];
	return out;
}

inline HmdMatrix34_t HmdMatrix_Multiply( const HmdMatrix34_t & left, const HmdMatrix34_t & right )
{
	HmdMatrix34_t out;	
	out.m[0][0] = 	left.m[0][0]*right.m[0][0] + left.m[0][1]*right.m[1][0] + left.m[0][2]*right.m[2][0] ;
	out.m[0][1] = 	left.m[0][0]*right.m[0][1] + left.m[0][1]*right.m[1][1] + left.m[0][2]*right.m[2][1] ;
	out.m[0][2] = 	left.m[0][0]*right.m[0][2] + left.m[0][1]*right.m[1][2] + left.m[0][2]*right.m[2][2] ;
	out.m[0][3] = 	left.m[0][0]*right.m[0][3] + left.m[0][1]*right.m[1][3] + left.m[0][2]*right.m[2][3] + left.m[0][3];

	out.m[1][0] = 	left.m[1][0]*right.m[0][0] + left.m[1][1]*right.m[1][0] + left.m[1][2]*right.m[2][0] ;
	out.m[1][1] = 	left.m[1][0]*right.m[0][1] + left.m[1][1]*right.m[1][1] + left.m[1][2]*right.m[2][1] ;
	out.m[1][2] = 	left.m[1][0]*right.m[0][2] + left.m[1][1]*right.m[1][2] + left.m[1][2]*right.m[2][2] ;
	out.m[1][3] = 	left.m[1][0]*right.m[0][3] + left.m[1][1]*right.m[1][3] + left.m[1][2]*right.m[2][3] + left.m[1][3];

	out.m[2][0] = 	left.m[2][0]*right.m[0][0] + left.m[2][1]*right.m[1][0] + left.m[2][2]*right.m[2][0] ;
	out.m[2][1] = 	left.m[2][0]*right.m[0][1] + left.m[2][1]*right.m[1][1] + left.m[2][2]*right.m[2][1] ;
	out.m[2][2] = 	left.m[2][0]*right.m[0][2] + left.m[2][1]*right.m[1][2] + left.m[2][2]*right.m[2][2] ;
	out.m[2][3] = 	left.m[2][0]*right.m[0][3] + left.m[2][1]*right.m[1][3] + left.m[2][2]*right.m[2][3] + left.m[2][3];

	return out;
}

inline HmdVector3_t HmdMatrix_Transform( const HmdMatrix34_t & mat, const HmdVector3_t & vec )
{
	HmdVector3_t out = {0};
	for( int i=0; i<3; i++ )
	{
		out.v[i] = mat.m[i][3];
		for( int j=0; j < 3; j++ )
		{
			out.v[i] += vec.v[j] * mat.m[i][j];
		}
	}

	return out;
}