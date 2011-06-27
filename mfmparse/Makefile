CC := gcc
CFLAGS := -g -Werror -Wall

TARGET := mfmparse mfmparse2adf kf2rawdat

all: $(TARGET)

kf2rawdat: kf2rawdat.c
mfmparse: mfmparse.c
mfmparse2adf: mfmparse2adf.c

clean:
	$(RM) $(TARGET) *.o *~
