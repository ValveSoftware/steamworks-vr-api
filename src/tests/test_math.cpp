//========= Copyright Valve Corporation ============//
// --------------------------------------------------------------------------------------
// Test cases for the pathtools module
// --------------------------------------------------------------------------------------


#include "gtest/gtest.h"

#include "vrcommon/hmdmatrixtools.h"


TEST( Math, VectorInit )
{
	HmdVector3_t vec = HmdVector_Init( 123.f, 456.f, 789.f );
	EXPECT_EQ( vec.v[0], 123.f );
	EXPECT_EQ( vec.v[1], 456.f );
	EXPECT_EQ( vec.v[2], 789.f );
}



TEST( Math, VectorDot )
{
	HmdVector3_t vec = HmdVector_Init( 1.f, 3.f, 5.f );
	HmdVector3_t vec2 = HmdVector_Init( 7.f, 9.f, -11.f );

	// should be 1 * 7 + 3 * 9 + 5 * -11 = -21
	EXPECT_DOUBLE_EQ( HmdVector_Dot( vec, vec2 ), -21.f );
}

TEST( Math, VectorCross1 )
{
	HmdVector3_t vec = HmdVector_Init( 0,0,1 );
	HmdVector3_t vec2 = HmdVector_Init( 0,1,0 );

	HmdVector3_t cross = HmdVector_Cross( vec, vec2 );
	EXPECT_DOUBLE_EQ( cross.v[0], -1 );
	EXPECT_DOUBLE_EQ( cross.v[1], 0 );
	EXPECT_DOUBLE_EQ( cross.v[2], 0 );
}

TEST( Math, VectorScalarMult )
{
	HmdVector3_t vec = HmdVector_Init( 1, 2, 3 );

	HmdVector3_t out = HmdVector_ScalarMultiply( vec, 5 );
	EXPECT_DOUBLE_EQ( out.v[0], 5 );
	EXPECT_DOUBLE_EQ( out.v[1], 10 );
	EXPECT_DOUBLE_EQ( out.v[2], 15 );
}

TEST( Math, VectorCross2 )
{
	HmdVector3_t vec = HmdVector_Init( 1,0,0 );
	HmdVector3_t vec2 = HmdVector_Init( 0,1,0 );

	HmdVector3_t cross = HmdVector_Cross( vec, vec2 );
	EXPECT_DOUBLE_EQ( cross.v[0], 0 );
	EXPECT_DOUBLE_EQ( cross.v[1], 0 );
	EXPECT_DOUBLE_EQ( cross.v[2], 1 );
}

TEST( Math, VectorCross3 )
{
	HmdVector3_t vec = HmdVector_Init( 0,0,1 );
	HmdVector3_t vec2 = HmdVector_Init( 1,0,0 );

	HmdVector3_t cross = HmdVector_Cross( vec, vec2 );
	EXPECT_DOUBLE_EQ( cross.v[0], 0 );
	EXPECT_DOUBLE_EQ( cross.v[1], 1 );
	EXPECT_DOUBLE_EQ( cross.v[2], 0 );
}

TEST( Math, VectorCross4 )
{
	HmdVector3_t vec = HmdVector_Init( 1, 2, 3 );
	HmdVector3_t vec2 = HmdVector_Init( 4, 5, 6 );

	HmdVector3_t cross = HmdVector_Cross( vec, vec2 );
	EXPECT_DOUBLE_EQ( cross.v[0], -3 );
	EXPECT_DOUBLE_EQ( cross.v[1], 6 );
	EXPECT_DOUBLE_EQ( cross.v[2], -3 );
}


TEST( Math, VectorNormalized )
{
	HmdVector3_t vec = HmdVector_Normalized( HmdVector_Init( 1.f, 4.f, 5.f ) );

	double fKnownLen = 6.4807406984078604;

	EXPECT_DOUBLE_EQ( vec.v[0], 1.0/fKnownLen );
	EXPECT_DOUBLE_EQ( vec.v[1], 4.0/fKnownLen );
	EXPECT_DOUBLE_EQ( vec.v[2], 5.0/fKnownLen );
}

TEST( Math, VectorNormalizedZero )
{
	HmdVector3_t vec = HmdVector_Normalized( HmdVector_Init( 0, 0, 0 ) );

	EXPECT_DOUBLE_EQ( vec.v[0], 0 );
	EXPECT_DOUBLE_EQ( vec.v[1], 0 );
	EXPECT_DOUBLE_EQ( vec.v[2], 0 );
}


TEST( Math, MatrixGetBasis )
{
	HmdMatrix34_t mat;
	mat.m[0][0] = 1.f;
	mat.m[1][0] = 2.f;
	mat.m[2][0] = 3.f;
	mat.m[0][1] = 4.f;
	mat.m[1][1] = 5.f;
	mat.m[2][1] = 6.f;
	mat.m[0][2] = 7.f;
	mat.m[1][2] = 8.f;
	mat.m[2][2] = 9.f;
	mat.m[0][3] = 10.f;
	mat.m[1][3] = 11.f;
	mat.m[2][3] = 12.f;

	EXPECT_TRUE( HmdVector_Equals( HmdMatrix_GetRight( mat ), HmdVector_Init( 1, 2, 3) ) );
	EXPECT_TRUE( HmdVector_Equals( HmdMatrix_GetUp( mat ), HmdVector_Init( 4, 5, 6 ) ) );
	EXPECT_TRUE( HmdVector_Equals( HmdMatrix_GetBack( mat ), HmdVector_Init( 7, 8, 9 ) ) );
	EXPECT_TRUE( HmdVector_Equals( HmdMatrix_GetTranslation( mat ), HmdVector_Init( 10, 11, 12 ) ) );
}

TEST( Math, MatrixComposeInit )
{
	HmdVector3_t right = HmdVector_Init( 1, 2, 3 );
	HmdVector3_t up = HmdVector_Init( 4, 5, 6 );
	HmdVector3_t back = HmdVector_Init( 7, 8, 9 );
	HmdVector3_t translation = HmdVector_Init( 10, 11, 12 );

	HmdMatrix34_t mat = HmdMatrix34_Init( right, up, back, translation );
	EXPECT_EQ( mat.m[0][0], 1.f );
	EXPECT_EQ( mat.m[1][0], 2.f );
	EXPECT_EQ( mat.m[2][0], 3.f );
	EXPECT_EQ( mat.m[0][1], 4.f );
	EXPECT_EQ( mat.m[1][1], 5.f );
	EXPECT_EQ( mat.m[2][1], 6.f );
	EXPECT_EQ( mat.m[0][2], 7.f );
	EXPECT_EQ( mat.m[1][2], 8.f );
	EXPECT_EQ( mat.m[2][2], 9.f );
	EXPECT_EQ( mat.m[0][3], 10.f );
	EXPECT_EQ( mat.m[1][3], 11.f );
	EXPECT_EQ( mat.m[2][3], 12.f );
}

TEST( Math, MatrixSetTranslation )
{
	HmdMatrix34_t mat;
	mat.m[0][0] = 1.f;
	mat.m[1][0] = 2.f;
	mat.m[2][0] = 3.f;
	mat.m[0][1] = 4.f;
	mat.m[1][1] = 5.f;
	mat.m[2][1] = 6.f;
	mat.m[0][2] = 7.f;
	mat.m[1][2] = 8.f;
	mat.m[2][2] = 9.f;
	mat.m[0][3] = 10.f;
	mat.m[1][3] = 11.f;
	mat.m[2][3] = 12.f;

	HmdMatrix_SetTranslationInline( &mat, HmdVector_Init( 111.f, 222.f, 333.f ) );

	EXPECT_EQ( mat.m[0][0], 1.f );
	EXPECT_EQ( mat.m[1][0], 2.f );
	EXPECT_EQ( mat.m[2][0], 3.f );
	EXPECT_EQ( mat.m[0][1], 4.f );
	EXPECT_EQ( mat.m[1][1], 5.f );
	EXPECT_EQ( mat.m[2][1], 6.f );
	EXPECT_EQ( mat.m[0][2], 7.f );
	EXPECT_EQ( mat.m[1][2], 8.f );
	EXPECT_EQ( mat.m[2][2], 9.f );
	EXPECT_EQ( mat.m[0][3], 111.f );
	EXPECT_EQ( mat.m[1][3], 222.f );
	EXPECT_EQ( mat.m[2][3], 333.f );
}

TEST( Math, MatrixTransform )
{
	HmdMatrix34_t mat;
	mat.m[0][0] = 1.f;
	mat.m[1][0] = 2.f;
	mat.m[2][0] = 3.f;
	mat.m[0][1] = 4.f;
	mat.m[1][1] = 5.f;
	mat.m[2][1] = 6.f;
	mat.m[0][2] = 7.f;
	mat.m[1][2] = 8.f;
	mat.m[2][2] = 9.f;
	mat.m[0][3] = 10.f;
	mat.m[1][3] = 11.f;
	mat.m[2][3] = 12.f;

	HmdVector3_t vec = HmdVector_Init( 4, 5, 6 );

	HmdVector3_t out = HmdMatrix_Transform( mat, vec );

	EXPECT_TRUE( HmdVector_Equals( out, HmdVector_Init( 76, 92, 108 ) ) );

}

TEST( Math, QuatConjugate )
{
	HmdQuaternion_t quat = HmdQuaternion_Init( 	0.356348322549899, 0.445435403187374, 0.534522483824849, 0.623609564462324 );
	
	quat = HmdQuaternion_Conjugate( quat );

	EXPECT_DOUBLE_EQ( 0.356348322549899 , quat.w );
	EXPECT_DOUBLE_EQ( -0.445435403187374 , quat.x );
	EXPECT_DOUBLE_EQ( -0.534522483824849 , quat.y );
	EXPECT_DOUBLE_EQ( -0.623609564462324 , quat.z );
}

TEST( Math, QuatRotateVector )
{
	HmdQuaternion_t quat = HmdQuaternion_Init(0.5332154482438284, 0.592817248117098, 0.08310956622699882, 0.5977807257603444);
	HmdVector3_t v = HmdVector_Init(0.6323592462254095, 0.09754040499940953, 0.2784982188670484);

	HmdVector3_t rotated = HmdQuaternion_RotateVector( quat, v );

	EXPECT_DOUBLE_EQ( 0.34118591372926310 , rotated.v[0] );
	EXPECT_DOUBLE_EQ( 0.27631329443403085 , rotated.v[1] );
	EXPECT_DOUBLE_EQ( 0.54239906010065664 , rotated.v[2] );
}


TEST( Math, QuatInit )
{
	HmdQuaternion_t quat = HmdQuaternion_Init( 	0.356348322549899, 0.445435403187374, 0.534522483824849, 0.623609564462324 );

	EXPECT_DOUBLE_EQ( 0.356348322549899 , quat.w );
	EXPECT_DOUBLE_EQ( 0.445435403187374 , quat.x );
	EXPECT_DOUBLE_EQ( 0.534522483824849 , quat.y );
	EXPECT_DOUBLE_EQ( 0.623609564462324 , quat.z );
}

TEST( Math, QuatFromAxisAngle )
{
	HmdVector3_t vecAxisAngle = HmdVector_Init( 0.814723686393179, 0.905791937075619, 0.126986816293506 );
	HmdQuaternion_t quat = HmdQuaternion_FromAxisAngleVector( vecAxisAngle );

	EXPECT_DOUBLE_EQ( 0.818244455718709 , quat.w );
	EXPECT_DOUBLE_EQ( 0.382368990975461 , quat.x );
	EXPECT_DOUBLE_EQ( 0.42510946324220233 , quat.y );
	EXPECT_DOUBLE_EQ( 0.0595979000295095 , quat.z );
}

TEST( Math, QuatProduct )
{
	HmdQuaternion_t quat1 = HmdQuaternion_Init( 0.356348322549899, 0.445435403187374, 0.534522483824849, 0.623609564462324 );
	HmdQuaternion_t quat2 = HmdQuaternion_Init( 0.818244455718709, 0.382368990975461, 0.425109463242202, 0.0595979000295095 );

	HmdQuaternion_t quatOut = HmdQuaternion_Multiply( quat1, quat2 );

	EXPECT_DOUBLE_EQ( -0.14313703310032849 , quatOut.w );
	EXPECT_DOUBLE_EQ( 0.267485687901336 , quatOut.x );
	EXPECT_DOUBLE_EQ( 0.800759048270463 , quatOut.y );
	EXPECT_DOUBLE_EQ( 0.51647666272172144 , quatOut.z );
}

TEST( Math, QuatToMatrix )
{
	HmdQuaternion_t quat = HmdQuaternion_Init( -0.143137033100328, 0.267485687901336, 0.800759048270463, 0.516476662721722 );
	HmdMatrix34_t mat = HmdQuaternion_ToRotationMatrix( quat );

	EXPECT_DOUBLE_EQ( -0.81592637300491333,	mat.m[0][0] );
	EXPECT_DOUBLE_EQ( 0.5762370228767395,	mat.m[0][1] );
	EXPECT_DOUBLE_EQ( 0.047063682228326797,	mat.m[0][2] );

	EXPECT_DOUBLE_EQ( 0.28052929043769836 ,	mat.m[1][0] );
	EXPECT_DOUBLE_EQ( 0.32340651750564575 ,	mat.m[1][1] );
	EXPECT_DOUBLE_EQ( 0.9037209153175354 ,	mat.m[1][2] );

	EXPECT_DOUBLE_EQ( 0.50553679466247559 ,	mat.m[2][0] );
	EXPECT_DOUBLE_EQ( 0.75057250261306763 ,	mat.m[2][1] );
	EXPECT_DOUBLE_EQ( -0.42552730441093445 ,mat.m[2][2] );

	EXPECT_DOUBLE_EQ( 0, mat.m[0][3] );
	EXPECT_DOUBLE_EQ( 0, mat.m[1][3] );
	EXPECT_DOUBLE_EQ( 0, mat.m[2][3] );
}


TEST( Math, QuatToMatrixIdent )
{
	HmdQuaternion_t quat = HmdQuaternion_Init( 1, 0, 0, 0 );
	HmdMatrix34_t mat = HmdQuaternion_ToRotationMatrix( quat );

	EXPECT_DOUBLE_EQ( 1 , mat.m[0][0] );
	EXPECT_DOUBLE_EQ( 0 , mat.m[1][0] );
	EXPECT_DOUBLE_EQ( 0 , mat.m[2][0] );

	EXPECT_DOUBLE_EQ( 0 , mat.m[0][1] );
	EXPECT_DOUBLE_EQ( 1 , mat.m[1][1] );
	EXPECT_DOUBLE_EQ( 0 , mat.m[2][1] );

	EXPECT_DOUBLE_EQ( 0 , mat.m[0][2] );
	EXPECT_DOUBLE_EQ( 0 , mat.m[1][2] );
	EXPECT_DOUBLE_EQ( 1 , mat.m[2][2] );

	EXPECT_DOUBLE_EQ( 0, mat.m[0][3] );
	EXPECT_DOUBLE_EQ( 0, mat.m[1][3] );
	EXPECT_DOUBLE_EQ( 0, mat.m[2][3] );
}

