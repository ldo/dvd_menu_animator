/*
	Python extension module for DVD Menu Animator script. This performs
	pixel-level manipulations that would be too slow done in Python.

	Created by Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
*/

#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

static PyObject * spuhelper_index_image
  (
	PyObject * self,
	PyObject * args
  )
  {
	PyObject * Result = 0;
	PyObject * ArrayModule = 0;
	PyObject * SrcArray = 0;
	PyObject * TheBufferInfo = 0;
	unsigned long pixaddr, nrpixbytes, pixlen;
	const uint32_t * pixels;
	typedef struct
	  {
		uint32_t pixel;
		unsigned long count;
	  } histentry;
	unsigned long nrhistentries = 0, maxhistentries, histindex;
	histentry * histogram = 0;
	PyObject * ResultArray = 0;
	PyObject * HistTuple = 0;
	do /*once*/
	  {
		ArrayModule = PyImport_ImportModule("array");
		if (ArrayModule == 0)
			break;
		if (!PyArg_ParseTuple(args, "O", &SrcArray))
			break;
		Py_INCREF(SrcArray);
		TheBufferInfo = PyObject_CallMethod(SrcArray, "buffer_info", "");
		if (TheBufferInfo == 0)
			break;
		if (!PyArg_ParseTuple(TheBufferInfo, "kk", &pixaddr, &nrpixbytes))
			break;
		pixels = (const uint32_t *)pixaddr;
		pixlen = nrpixbytes >> 2;
		for (;;)
		  {
			if (pixlen == 0)
				break;
			const uint32_t thispixel = *pixels;
			if (histogram == 0)
			  {
				maxhistentries = 8; /* something convenient to start with */
				histogram = malloc(maxhistentries * sizeof(histentry));
				if (histogram == 0)
				  {
					PyErr_NoMemory();
					break;
				  } /*if*/
				nrhistentries = 0;
			  } /*if*/
			histindex = 0;
			for (;;)
			  {
			  /* look through histogram to see if I've seen this pixel value before */
			  /* maybe try binary search and ordered histogram to speed things up? */
				if (histindex == nrhistentries)
				  {
				  /* pixel not seen before, add new histogram entry */
					if (nrhistentries == maxhistentries)
					  {
					  /* preallocation filled, need to extend it */
						maxhistentries *= 2; /* try to avoid too many reallocations */
						histogram = realloc(histogram, maxhistentries * sizeof(histentry));
						if (histogram == 0)
						  {
							PyErr_NoMemory();
							break;
						  } /*if*/
					  } /*if*/
					histogram[nrhistentries].pixel = thispixel;
					histogram[nrhistentries].count = 1;
					++nrhistentries;
					break;
				  } /*if*/
				if (histogram[histindex].pixel == thispixel)
				  {
				  /* another occurrence of this pixel value */
					++histogram[histindex].count;
					break;
				  } /*if*/
				++histindex;
			  } /*for*/
			if (PyErr_Occurred())
				break;
			++pixels;
			--pixlen;
		  } /*for*/
		if (PyErr_Occurred())
			break;
		if (nrhistentries <= 4)
		  {
			const size_t PixBufSize = 128 /* convenient buffer size to avoid heap allocation */;
			const size_t MaxPixels = PixBufSize * 4; /* 2 bits per pixel */
			uint8_t PixBuf[PixBufSize];
			size_t NrPixels;
			ResultArray = PyObject_CallMethod(ArrayModule, "array", "s", "B");
			if (ResultArray == 0)
				break;
			pixels = (const uint32_t *)pixaddr;
			pixlen = nrpixbytes >> 2;
			NrPixels = 0;
			for (;;)
			  {
			  /* extend ResultArray by a bunch of converted pixels at a time */
				if (pixlen == 0 || NrPixels == MaxPixels)
				  {
					PyObject * BufString = 0;
					PyObject * Result = 0;
					do /*once*/
					  {
						if (NrPixels == 0)
							break; /* nothing to flush out */
						if (NrPixels % 4 != 0)
						  {
						  /* fill out unused part of byte with zeroes */
							PixBuf[NrPixels / 4] &= ~(0xff << NrPixels % 4 * 2);
						  } /*if*/
						BufString = PyString_FromStringAndSize((const char *)PixBuf, (NrPixels + 3) / 4);
						if (BufString == 0)
							break;
						Result = PyObject_CallMethod(ResultArray, "fromstring", "O", BufString);
						if (Result == 0)
							break;
					  }
					while (false);
					Py_XDECREF(Result);
					Py_XDECREF(BufString);
					if (PyErr_Occurred())
						break;
					if (pixlen == 0)
						break;
				  } /*if*/
				const uint32_t thispixel = *pixels;
				for (histindex = 0; histogram[histindex].pixel != thispixel; ++histindex)
				  /* guaranteed to find pixel index */;
				if (NrPixels % 4 == 0)
				  {
				  /* starting new byte */
					PixBuf[NrPixels / 4] = 0; /* ensure there's no junk in it */
				  } /*if*/
				PixBuf[NrPixels / 4] |= histindex << NrPixels % 4 * 2;
				++NrPixels;
				++pixels;
				--pixlen;
			  } /*for*/
			if (PyErr_Occurred())
				break;
		  }
		else
		  {
		  /* don't bother building an indexed version of image */
			Py_INCREF(Py_None);
			ResultArray = Py_None;
		  } /*if*/
		HistTuple = PyTuple_New(nrhistentries);
		if (HistTuple == 0)
			break;
		for (histindex = 0;;)
		  {
		  /* fill in HistTuple */
			if (histindex == nrhistentries)
				break;
			PyObject * HistEntryTuple = 0;
			PyObject * ColorTuple = 0;
			PyObject * Count = 0;
			do /*once*/
			  {
				const uint32_t pixel = histogram[histindex].pixel;
				ColorTuple = PyTuple_New(4);
				if (ColorTuple == 0)
					break;
				PyTuple_SET_ITEM(ColorTuple, 0, PyInt_FromLong(pixel >> 16 & 255)); /* R */
				PyTuple_SET_ITEM(ColorTuple, 1, PyInt_FromLong(pixel >> 8 & 255)); /* G */
				PyTuple_SET_ITEM(ColorTuple, 2, PyInt_FromLong(pixel & 255)); /* B */
				PyTuple_SET_ITEM(ColorTuple, 3, PyInt_FromLong(pixel >> 24 & 255)); /* A */
				Count = PyInt_FromLong(histogram[histindex].count);
				HistEntryTuple = PyTuple_New(2);
				if (PyErr_Occurred())
					break;
				PyTuple_SET_ITEM(HistEntryTuple, 0, ColorTuple);
				PyTuple_SET_ITEM(HistEntryTuple, 1, Count);
				PyTuple_SET_ITEM(HistTuple, histindex, HistEntryTuple);
			  /* lose stolen references */
				ColorTuple = 0;
				Count = 0;
				HistEntryTuple = 0;
			  }
			while (false);
			Py_XDECREF(Count);
			Py_XDECREF(ColorTuple);
			Py_XDECREF(HistEntryTuple);
			if (PyErr_Occurred())
				break;
			++histindex;
		  } /*for*/
		if (PyErr_Occurred())
			break;
	  /* all done */
		Result = Py_BuildValue("OO", ResultArray, HistTuple);
	  }
	while (false);
	Py_XDECREF(HistTuple);
	Py_XDECREF(ResultArray);
	free(histogram);
	Py_XDECREF(TheBufferInfo);
	Py_XDECREF(SrcArray);
	Py_XDECREF(ArrayModule);
	return Result;
  } /*spuhelper_index_image*/

static PyObject * spuhelper_cairo_to_gtk
  (
	PyObject * self,
	PyObject * args
  )
  /* converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering to
	GTK Pixbuf (big-endian) ordering. */
  {
	PyObject * result = 0;
	PyObject * TheArray = 0;
	PyObject * TheBufferInfo = 0;
	unsigned long pixaddr, pixlen;
	uint8_t * pixels;
	do /*once*/
	  {
		if (!PyArg_ParseTuple(args, "O", &TheArray))
			break;
		Py_INCREF(TheArray);
		TheBufferInfo = PyObject_CallMethod(TheArray, "buffer_info", "");
		if (TheBufferInfo == 0)
			break;
		if (!PyArg_ParseTuple(TheBufferInfo, "kk", &pixaddr, &pixlen))
			break;
		pixels = (uint8_t *)pixaddr;
		pixlen >>= 2;
		while (pixlen > 0)
		  {
			const uint32_t thispixel = *(const uint32_t *)pixels;
			*pixels++ = thispixel >> 16 & 255; /* R */
			*pixels++ = thispixel >> 8 & 255; /* G */
			*pixels++ = thispixel & 255; /* B */
			*pixels++ = thispixel >> 24 & 255; /* alpha */
			--pixlen;
		  } /*while*/
	  /* all done */
		Py_INCREF(Py_None);
		result = Py_None;
	  }
	while (false);
	Py_XDECREF(TheBufferInfo);
	Py_XDECREF(TheArray);
	return result;
  } /*spuhelper_cairo_to_gtk*/

static PyMethodDef spuhelper_methods[] =
  {
	{"index_image", spuhelper_index_image, METH_VARARGS,
		"index_image(array)\n"
		"analyzes a buffer of RGBA-format pixels in Cairo (native-endian) ordering, and"
		" returns a tuple of 2 elements, the first being a new array object containing"
		" 2 bits per pixel, and the second being a tuple of corresponding colours."
	},
	{"cairo_to_gtk", spuhelper_cairo_to_gtk, METH_VARARGS,
		"cairo_to_gtk(array)\n"
		"converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering"
		" to GTK Pixbuf (big-endian) ordering."
	},
	{0, 0, 0, 0} /* marks end of list */
  };

PyMODINIT_FUNC initspuhelper(void)
  {
	(void)Py_InitModule("spuhelper", spuhelper_methods);
  } /*initspuhelper*/
