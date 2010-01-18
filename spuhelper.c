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
	PyObject * result = 0;
	unsigned long pixaddr, pixlen;
	const uint32_t * pixels;
	do /*once*/
	  {
		if (!PyArg_ParseTuple(args, "kk", &pixaddr, &pixlen))
			break;
		pixels = (const uint32_t *)pixaddr;
		pixlen >>= 2;
	  /* more TBD */
	  /* all done */
		Py_INCREF(Py_None);
		result = Py_None;
	  }
	while (false);
	return result;
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
#if 0
	{"index_image", spuhelper_index_image, METH_VARRGS,
		"index_image(address, nrbytes)\n"
		"analyzes a buffer of RGBA-format pixels in Cairo (native-endian) ordering, and"
		" returns a tuple of 2 elements, the first being a new array object containing"
		" 2 bits per pixel, and the second being a tuple of corresponding colours."
	},
#endif
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
