/*
	Python extension module for DVD Menu Animator script. This performs
	pixel-level manipulations that would be too slow done in Python.

	Created by Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.
*/

#include <Python.h>
#include <stdbool.h>
#include <stdint.h>

/*
	Miscellaneous useful stuff
*/

typedef struct
  {
	unsigned long count;
	uint32_t pixel;
  } histentry;

static void sort_hist_by_count
  (
	histentry * histogram,
	unsigned long nrhistentries
  )
  /* does a Shellsort sort on the elements in a list. This is
	not a stable sort. */
  {
	ulong i, j, k, l, m;
	histentry swaptemp;
	m = 1;
	while (m < nrhistentries)
	  {
		m = m + m;
	  } /*while*/
	m =
			(m - 1)
			  /* to ensure that successive increments are relatively prime,
				as per Knuth vol 3 page 91 */
		>>
			1;
	if (m == 0 && nrhistentries > 1)
	  {
	  /* do at least one pass when sorting 2 elements */
		m = 1;
	  } /*if*/
	while (m > 0)
	  {
		k = nrhistentries - m;
		for (j = 0; j < k; ++j)
		  {
			i = j;
			for (;;)
			  {
				l = i + m;
				if (histogram[i].count >= histogram[l].count)
					break;
				swaptemp = histogram[i];
				histogram[i] = histogram[l];
				histogram[l] = swaptemp;
				if (i <= m)
					break;
				i -= m;
			  } /*for*/
		  } /*for*/
		m >>= 1;
	  } /*while*/
  } /*sort_hist_by_count*/

static void GetBufferInfo
  (
	PyObject * FromArray,
	unsigned long * addr,
	unsigned long * len
  )
  /* returns the address and length of the data in a Python array object. */
  {
	PyObject * TheBufferInfo = 0;
	PyObject * AddrObj = 0;
	PyObject * LenObj = 0;
	do /*once*/
	  {
		TheBufferInfo = PyObject_CallMethod(FromArray, "buffer_info", "");
		if (TheBufferInfo == 0)
			break;
		AddrObj = PyTuple_GetItem(TheBufferInfo, 0);
		LenObj = PyTuple_GetItem(TheBufferInfo, 1);
		if (PyErr_Occurred())
			break;
		Py_INCREF(AddrObj);
		Py_INCREF(LenObj);
		*addr = PyInt_AsUnsignedLongMask(AddrObj);
		*len = PyInt_AsUnsignedLongMask(LenObj);
		if (PyErr_Occurred())
			break;
	  }
	while (false);
	Py_XDECREF(AddrObj);
	Py_XDECREF(LenObj);
	Py_XDECREF(TheBufferInfo);
  } /*GetBufferInfo*/

/*
	User-visible stuff
*/

static PyObject * spuhelper_index_image
  (
	PyObject * self,
	PyObject * args
  )
  /* computes a histogram of pixel frequency on an image, and also generates a
	two-bit-per-pixel indexed version of the image if possible. */
  {
	const unsigned long count_factor = 50;
	  /* ignore excess colours provided they make up no more than a proportion
		1 / count_factor of the pixels */
	PyObject * Result = 0;
	PyObject * ArrayModule = 0;
	PyObject * SrcArray = 0;
	unsigned long pixaddr, nrpixbytes, srcrowstride, nrpixels, pixlen;
	const uint32_t * pixels;
	unsigned long nrhistentries = 0, maxhistentries, histindex;
	histentry * histogram = 0;
	PyObject * ResultArray = 0;
	PyObject * HistTuple = 0;
	uint8_t * PrevRow = 0;
	uint8_t * CurRow = 0;
	do /*once*/
	  {
		ArrayModule = PyImport_ImportModule("array");
		if (ArrayModule == 0)
			break;
		if (!PyArg_ParseTuple(args, "Ok", &SrcArray, &srcrowstride))
			break;
		Py_INCREF(SrcArray);
		GetBufferInfo(SrcArray, &pixaddr, &nrpixbytes);
		if (PyErr_Occurred())
			break;
		pixels = (const uint32_t *)pixaddr;
		nrpixels = nrpixbytes >> 2;
		pixlen = nrpixels;
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
		sort_hist_by_count(histogram, nrhistentries);
		if
		  (
				nrhistentries <= 4
			||
						nrpixels
					/
						(
							nrpixels
						-
							(
								histogram[0].count
							+
								histogram[1].count
							+
								histogram[2].count
							+
								histogram[3].count
							)
						) /* won't be zero */
				>=
					count_factor
		  )
		  {
		  /* preponderance of no more than four colours in image, rest can be put down
			to anti-aliasing that I have to undo */
			  {
			  /* generate indexed version of image */
				const size_t dstrowstride = (srcrowstride / 4 + 3) / 4;
				const size_t MaxPixels = srcrowstride / 4;
				size_t BufPixels;
				bool FirstRow = true;
				const unsigned long maxpixsearch = nrhistentries > 4 ? 4 : nrhistentries;
				ResultArray = PyObject_CallMethod(ArrayModule, "array", "s", "B");
				if (ResultArray == 0)
					break;
				PrevRow = malloc(dstrowstride);
				  /* need to keep previous row of converted pixels for neighbour comparison */
				if (PrevRow == 0)
				  {
					PyErr_NoMemory();
					break;
				  } /*if*/
				CurRow = malloc(dstrowstride);
				if (CurRow == 0)
				  {
					PyErr_NoMemory();
					break;
				  } /*if*/
				pixels = (const uint32_t *)pixaddr;
				pixlen = nrpixels;
				BufPixels = 0;
				for (;;)
				  {
				  /* extend ResultArray by a row of converted pixels at a time */
					if (pixlen == 0 || BufPixels == MaxPixels)
					  {
						PyObject * BufString = 0;
						PyObject * Result = 0;
						uint8_t * SwapTemp;
						do /*once*/
						  {
							if (BufPixels == 0)
								break; /* nothing to flush out */
							if (BufPixels % 4 != 0)
							  {
							  /* fill out unused part of byte with zeroes--actually shouldn't occur */
								CurRow[BufPixels / 4] &= ~(0xff << BufPixels % 4 * 2);
							  } /*if*/
							BufString = PyString_FromStringAndSize((const char *)CurRow, dstrowstride);
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
						SwapTemp = PrevRow;
						PrevRow = CurRow;
						CurRow = SwapTemp;
						BufPixels = 0;
						FirstRow = false;
					  } /*if*/
					const uint32_t thispixel = *pixels;
					if (BufPixels % 4 == 0)
					  {
					  /* starting new byte */
						CurRow[BufPixels / 4] = 0; /* ensure there's no junk in it */
					  } /*if*/
					for (histindex = 0;;)
					  {
						if (histindex == maxpixsearch)
							break;
						if (histogram[histindex].pixel == thispixel)
							break;
						++histindex;
					  } /*for*/
					if (histindex == 4)
					  {
					  /* not one of the most popular colours, replace with the most popular
						of its neighbours. Remember I only need to do this for a minority
						of pixels, based on count_factor. */
						unsigned long bestweight = 0; /* all weights are guaranteed greater than this */
					  /* look only at neighbours I've already processed, otherwise this
						will need at least two passes */
						if (BufPixels > 0 && !FirstRow)
						  {
						  /* there is an upper-left neighbour */
							const unsigned int upperleft_neighbour =
										PrevRow[BufPixels - (BufPixels % 4 == 0 ? 1 : 0)]
									>>
										(BufPixels % 4 == 0 ? 6 : (BufPixels % 4 - 1) * 2)
								&
									3;
							if (histogram[upperleft_neighbour].count > bestweight)
							  {
								histindex = upperleft_neighbour;
								bestweight = histogram[upperleft_neighbour].count;
							  } /*if*/
						  } /*if*/
						if (!FirstRow)
						  {
						  /* there is an upper neighbour */
							const unsigned int upper_neighbour =
								PrevRow[BufPixels] >> BufPixels * 2 & 3;
							if (histogram[upper_neighbour].count > bestweight)
							  {
								histindex = upper_neighbour;
								bestweight = histogram[upper_neighbour].count;
							  } /*if*/
						  } /*if*/
						if (BufPixels > 0)
						  {
						  /* there is a left neighbour */
							const unsigned int left_neighbour =
										CurRow[BufPixels - (BufPixels % 4 == 0 ? 1 : 0)]
									>>
										(BufPixels % 4 == 0 ? 6 : (BufPixels % 4 - 1) * 2)
								&
									3;
							if (histogram[left_neighbour].count > bestweight)
							  {
								histindex = left_neighbour;
								bestweight = histogram[left_neighbour].count;
							  } /*if*/
							if (bestweight == 0)
							  {
							  /* very first pixel has no neighbours matching above
								criteria. What to do if this is antialiased? Fudge it. */
								histindex = 0;
							  } /*if*/
						  } /*if*/
					  } /*if*/
					CurRow[BufPixels / 4] |= histindex << BufPixels % 4 * 2;
					++BufPixels;
					++pixels;
					--pixlen;
				  } /*for*/
			  }
			if (PyErr_Occurred())
				break;
		  }
		else
		  {
		  /* too many different colours, don't bother building an indexed version of image */
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
	free(PrevRow);
	free(CurRow);
	Py_XDECREF(HistTuple);
	Py_XDECREF(ResultArray);
	free(histogram);
	Py_XDECREF(SrcArray);
	Py_XDECREF(ArrayModule);
	return Result;
  } /*spuhelper_index_image*/

static PyObject * spuhelper_expand_image
  (
	PyObject * self,
	PyObject * args
  )
  /* expands a 2-bit-per-pixel image, substituting the specified colours. */
  {
	PyObject * Result = 0;
	PyObject * SrcArray = 0;
	PyObject * ColorTuple = 0;
	PyObject * ArrayModule = 0;
	PyObject * DstArray = 0;
	unsigned long srcpixaddr, nrsrcpixbytes;
	uint32_t colors[4];
	do /*once*/
	  {
		if (!PyArg_ParseTuple(args, "OO", &SrcArray, &ColorTuple))
			break;
		Py_INCREF(SrcArray);
		Py_INCREF(ColorTuple);
		ArrayModule = PyImport_ImportModule("array");
		if (ArrayModule == 0)
			break;
		GetBufferInfo(SrcArray, &srcpixaddr, &nrsrcpixbytes);
		if (PyErr_Occurred())
			break;
		  {
		  /* parse the colour specifications */
			PyObject * TheColors[4] = {0, 0, 0, 0};
			unsigned int i, j, channel[4];
			do /*once*/
			  {
				for (i = 0; i < 4; ++i)
				  {
					TheColors[i] = PyTuple_GetItem(ColorTuple, i);
				  } /*for*/
				if (PyErr_Occurred())
					break;
				for (i = 0; i < 4; ++i)
				  {
					Py_INCREF(TheColors[i]);
				  } /*for*/
				for (i = 0;;)
				  {
					if (i == 4)
						break;
					for (j = 0;;)
					  {
						if (j == 4)
							break;
						PyObject * ColorObj = PyTuple_GetItem(TheColors[i], j);
						if (PyErr_Occurred())
							break;
						const long chanval = PyInt_AsLong(ColorObj);
						if (PyErr_Occurred())
							break;
						if (chanval < 0 || chanval > 255)
						  {
							PyErr_SetString
							  (
								PyExc_ValueError,
								"colour components must be in [0 .. 255]"
							  );
							break;
						  } /*if*/
						channel[(j + 1) % 4] = chanval;
						++j;
					  } /*for*/
					if (PyErr_Occurred())
						break;
					colors[i] =
							channel[0] << 24
						|
							channel[1] << 16
						|
							channel[2] << 8
						|
							channel[3];
					++i;
				  } /*for*/
				if (PyErr_Occurred())
					break;
			  }
			while (false);
			for (i = 0; i < 4; ++i)
			  {
				Py_XDECREF(TheColors[i]);
			  } /*for*/
			if (PyErr_Occurred())
				break;
		  }
		DstArray = PyObject_CallMethod(ArrayModule, "array", "s", "B");
		if (DstArray == 0)
			break;
		  {
		  /* expand the pixels */
			const unsigned long MaxBufPixels = 128 /* convenient buffer size to avoid heap allocation */;
			uint32_t PixBuf[MaxBufPixels];
			const uint8_t * SrcPixels = (const uint8_t *)srcpixaddr;
			unsigned long NrBufPixels, NrSrcPixels, SrcPixIndex;
			unsigned int SrcPix;
			NrSrcPixels = nrsrcpixbytes << 2;
			SrcPixIndex = 0;
			NrBufPixels = 0;
			for (;;)
			  {
				if (NrSrcPixels == 0 || NrBufPixels == MaxBufPixels)
				  {
					PyObject * BufString = 0;
					PyObject * Result = 0;
					do /*once*/
					  {
						if (NrBufPixels == 0)
							break; /* nothing to flush out */
						BufString = PyString_FromStringAndSize((const char *)PixBuf, NrBufPixels * 4);
						if (BufString == 0)
							break;
						Result = PyObject_CallMethod(DstArray, "fromstring", "O", BufString);
						if (Result == 0)
							break;
					  }
					while (false);
					Py_XDECREF(Result);
					Py_XDECREF(BufString);
					if (PyErr_Occurred())
						break;
					if (NrSrcPixels == 0)
						break;
					NrBufPixels = 0;
				  } /*if*/
				if (SrcPixIndex == 0)
				  {
					SrcPix = *SrcPixels;
				  } /*if*/
				PixBuf[NrBufPixels] = colors[SrcPix >> SrcPixIndex * 2 & 3];
				++NrBufPixels;
				++SrcPixIndex;
				if (SrcPixIndex == 4)
				  {
					++SrcPixels;
					SrcPixIndex = 0;
				  } /*if*/
				--NrSrcPixels;
			  } /*for*/
			if (PyErr_Occurred())
				break;
		  }
	  /* all done */
		Result = DstArray;
		DstArray = 0; /* so I don't dispose of it yet */
	  }
	while (false);
	Py_XDECREF(DstArray);
	Py_XDECREF(ArrayModule);
	Py_XDECREF(ColorTuple);
	Py_XDECREF(SrcArray);
	return Result;
  } /*spuhelper_expand_image*/

static PyObject * spuhelper_cairo_to_gtk
  (
	PyObject * self,
	PyObject * args
  )
  /* converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering to
	GTK Pixbuf (big-endian) ordering. I guess this function isn't directly related
	to the other two, but where else to put it? */
  {
	PyObject * result = 0;
	PyObject * TheArray = 0;
	unsigned long pixaddr, pixlen;
	uint8_t * pixels;
	do /*once*/
	  {
		if (!PyArg_ParseTuple(args, "O", &TheArray))
			break;
		Py_INCREF(TheArray);
		GetBufferInfo(TheArray, &pixaddr, &pixlen);
		if (PyErr_Occurred())
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
	Py_XDECREF(TheArray);
	return result;
  } /*spuhelper_cairo_to_gtk*/

static PyMethodDef spuhelper_methods[] =
  {
	{"index_image", spuhelper_index_image, METH_VARARGS,
		"index_image(array, row_stride)\n"
		"analyzes a buffer of RGBA-format pixels in Cairo (native-endian) ordering, and"
		" returns a tuple of 2 elements, the first being a new array object containing"
		" 2 bits per pixel, and the second being a tuple of corresponding colours."
		" The number of pixels must be a multiple of 4."
	},
	{"expand_image", spuhelper_expand_image, METH_VARARGS,
		"expand_image(array, colors)\n"
		"expands a 2-bit-per-pixel image as previously generated by index_image,"
		" substituting the specified colours. Returns an array object containing"
		" 32 bits per pixel in Cairo (native-endian) ordering."
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
	(void)Py_InitModule3("spuhelper", spuhelper_methods,
		"helper functions for dvd_menu_animator script");
  } /*initspuhelper*/
