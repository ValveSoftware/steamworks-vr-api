//========= Copyright Valve Corporation ============//
// --------------------------------------------------------------------------------------
// Test cases for the pathtools module
// --------------------------------------------------------------------------------------


#include "gtest/gtest.h"

#include "vrcommon/pathtools.h"


TEST( PathTools, GetExecutablePath )
{
	ASSERT_FALSE( Path_GetExecutablePath().empty() );
}

TEST( PathTools, GetSlash )
{
#if defined( _WIN32 )
	ASSERT_EQ( '\\', Path_GetSlash() );
#else
	ASSERT_EQ( '/', Path_GetSlash() );
#endif
}

TEST( PathTools, FixSlashes )
{
	EXPECT_STREQ( "foo/bar/baz", Path_FixSlashes( "foo\\bar/baz", '/' ).c_str() );
	EXPECT_STREQ( "/foo/bar/baz", Path_FixSlashes( "/foo\\bar/baz", '/' ).c_str() );
	EXPECT_STREQ( "/foo/bar/baz", Path_FixSlashes( "\\foo\\bar/baz", '/' ).c_str() );
}

TEST( PathTools, StripFilename )
{
	EXPECT_STREQ( "foo/bar", Path_StripFilename( "foo/bar/baz", '/' ).c_str() );
	EXPECT_STREQ( "foo/bar", Path_StripFilename( "foo/bar/", '/' ).c_str() );
	EXPECT_STREQ( "foo.txt", Path_StripFilename( "foo.txt", '/' ).c_str() );
}

TEST( PathTools, IsAbsolute )
{
	EXPECT_TRUE( Path_IsAbsolute( "/something/long/and/gnarly.txt" ) );
	EXPECT_TRUE( Path_IsAbsolute( "\\something\\long\\and\\gnarly.txt" ) );
	EXPECT_TRUE( Path_IsAbsolute( "/simplefile.txt" ) );
	EXPECT_TRUE( Path_IsAbsolute( "c:\\simplefile.txt" ) );
	EXPECT_FALSE( Path_IsAbsolute( "simplefile.txt" ) );
	EXPECT_FALSE( Path_IsAbsolute( "..\\simplefile.txt" ) );
	EXPECT_FALSE( Path_IsAbsolute( "../simplefile.txt" ) );
	EXPECT_FALSE( Path_IsAbsolute( "dirname/simplefile.txt" ) );
}

TEST( PathTools, Join )
{
	EXPECT_STREQ( "dir/file.txt", Path_Join( "dir", "file.txt", '/' ).c_str() );
	EXPECT_STREQ( "dir/file.txt", Path_Join( "dir/", "file.txt", '/' ).c_str() );
}


TEST( PathTools, Compact )
{
	EXPECT_STREQ( "dir/file.txt", Path_Compact( "dir/something/../file.txt", '/' ).c_str() );
	EXPECT_STREQ( "../file.txt", Path_Compact( "dir/something/../../../file.txt", '/' ).c_str() );
	EXPECT_STREQ( "file.txt", Path_Compact( "./file.txt", '/' ).c_str() );
	EXPECT_STREQ( "somedir/file.txt", Path_Compact( "somedir/././././file.txt", '/' ).c_str() );
	EXPECT_STREQ( "somedir/file.txt", Path_Compact( "somedir/./file.txt", '/' ).c_str() );
	EXPECT_STREQ( "somedir/", Path_Compact( "somedir/.", '/' ).c_str() );
}


TEST( PathTools, MakeAbsolute )
{
	EXPECT_STREQ( "", Path_MakeAbsolute( "some/relative/path.tst", "another/relative/path", '/' ).c_str() );
	EXPECT_STREQ( "/an/absolute/path/some/relative/path.tst", Path_MakeAbsolute( "some/relative/path.tst", "/an/absolute/path", '/' ).c_str() );
	EXPECT_STREQ( "/some/absolute/path.tst", Path_MakeAbsolute( "/some/absolute/path.tst", "/an/absolute/path", '/' ).c_str() );
	EXPECT_STREQ( "", Path_MakeAbsolute( "../../path.tst", "/shortdir", '/' ).c_str() );
}
