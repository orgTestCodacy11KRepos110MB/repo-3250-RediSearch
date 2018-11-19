#include <gtest/gtest.h>
#include <util/arr.h>

class ArrTest : public ::testing::Test {};

typedef struct Foo {
  int x;
  double y;
} Foo;

TEST_F(ArrTest, testStruct) {
  Foo *arr = (Foo *)array_new(Foo, 8);

  for (int i = 0; i < 10; i++) {
    arr = (Foo *)array_append(arr, (Foo){i});
    ASSERT_EQ(i + 1, array_len(arr));
  }

  for (int i = 0; i < 10; i++) {
    ASSERT_EQ(i, arr[i].x);
  }
  array_foreach(arr, elem, printf("%d\n", elem.x));
  array_free(arr);
}

TEST_F(ArrTest, testScalar) {
  int *ia = array_new(int, 8);
  for (int i = 0; i < 100; i++) {
    ia = array_append(ia, i);
    ASSERT_EQ(i + 1, array_len(ia));
    ASSERT_EQ(i, array_tail(ia));
  }

  for (int i = 0; i < array_len(ia); i++) {
    ASSERT_EQ(i, ia[i]);

    printf("%d %zd\n", ia[i], array_len(ia));
  }
  array_free(ia);
}

TEST_F(ArrTest, testStrings) {
  const char *strs[] = {"foo", "bar", "baz", NULL};
  char **a = array_new(char *, 1);
  int i = 0;
  for (i = 0; strs[i] != NULL; i++) {
    a = array_append(a, strdup(strs[i]));
    ASSERT_EQ(i + 1, array_len(a));
    ASSERT_STREQ(strs[i], array_tail(a));
  }
  for (int j = 0; j < i; j++) {
    ASSERT_STREQ(strs[j], a[j]);

    // printf("%s\n", a[j]);
  }
  array_free_ex(a, free(*(void **)ptr));
}

TEST_F(ArrTest, testTrimm) {
  const char *strs[] = {"foo", "bar", "baz", NULL};
  const char **a = array_new(const char *, 16);
  int i = 0;
  for (i = 0; strs[i] != NULL; i++) {
    a = array_append(a, strs[i]);
    ASSERT_EQ(i + 1, array_len(a));
    ASSERT_STREQ(strs[i], array_tail(a));
  }
  a = array_trimm_cap(a, 2);
  ASSERT_EQ(array_len(a), 2);
  array_trimm_len(a, 1);
  ASSERT_EQ(array_len(a), 1);
  array_free(a);
}

TEST_F(ArrTest, testEnsure) {
  Foo *f = array_new(Foo, 1);
  array_hdr_t *hdr = array_hdr(f);
  Foo *tail = array_ensure_tail(&f, Foo);
  // Make sure Valgrind does not complain!
  tail->x = 0;
  tail->y = 0;

  Foo *middle = array_ensure_at(&f, 5, Foo);
  ASSERT_EQ(0, middle->x);
  ASSERT_EQ(0, middle->y);

  for (size_t ii = 0; ii < array_len(f); ++ii) {
    ASSERT_EQ(0, f[ii].x);
    ASSERT_EQ(0, f[ii].y);
  }
  array_free(f);
}