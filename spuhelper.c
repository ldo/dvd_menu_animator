/*
    Python extension module for DVD Menu Animator script. This performs
    pixel-level manipulations that would be too slow done in Python.

    Copyright 2010, 2015 Lawrence D'Oliveiro <ldo@geek-central.gen.nz>.

    This file is part of DVD Menu Animator.

    DVD Menu Animator is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by the
    Free Software Foundation, either version 3 of the License, or (at your
    option) any later version.

    DVD Menu Animator is distributed in the hope that it will be useful, but
    WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
    or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
    for more details.

    You should have received a copy of the GNU General Public License
    along with DVD Menu Animator. If not, see <http://www.gnu.org/licenses/>.

*/

#include <Python.h>
#include <stdbool.h>
#include <stdint.h>
#define PNG_SKIP_SETJMP_CHECK
#include <png.h>

/*
    Miscellaneous useful stuff
*/

static void extract_hsv
  (
    uint32_t pixel, /* cairo ARGB format */
    unsigned int * h,
    unsigned int * s,
    unsigned int * v
  )
  /* converts the RGB colour of the pixel to H, S and V components. */
  {
    const unsigned int
        r = pixel >> 16 & 255,
        g = pixel >> 8 & 255,
        b = pixel & 255;
    int v0, v1, v2, hoffset;
    if (r >= g && r >= b)
      {
        v0 = r;
        v1 = g;
        v2 = b;
        hoffset = 0;
      }
    else if (g >= r && g >= b)
      {
        v0 = g;
        v1 = b;
        v2 = r;
        hoffset = 65536 / 3;
      }
    else /* b >= r && b >= g */
      {
        v0 = b;
        v1 = r;
        v2 = g;
        hoffset = 65536 * 2 / 3;
      } /*if*/
    if (v0 != 0)
      {
        *h = (hoffset + 65536 + (v1 - v2) * 65536 / 6 / v0) % 65536;
        *s = (v0 - (v1 < v2 ? v1 : v2)) * 65536 / v0;
      }
    else /* v1, v2 also 0 */
      {
        *h = 0;
        *v = 0;
      } /*if*/
    *v = v0 * 257;
  } /*extract_hsv*/

typedef struct
  {
    unsigned long count;
    uint32_t pixel;
    unsigned short index;
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
                if (i < m)
                    break;
                i -= m;
              } /*for*/
          } /*for*/
        m >>= 1;
      } /*while*/
  } /*sort_hist_by_count*/

static void get_buffer_info
  (
    PyObject * from_array,
    unsigned long * addr,
    unsigned long * len
  )
  /* returns the address and length of the data in a Python array object. */
  {
    PyObject * the_buffer_info = 0;
    PyObject * addr_obj = 0;
    PyObject * len_obj = 0;
    do /*once*/
      {
        the_buffer_info = PyObject_CallMethod(from_array, "buffer_info", "");
        if (the_buffer_info == 0)
            break;
        addr_obj = PyTuple_GetItem(the_buffer_info, 0);
        if (PyErr_Occurred())
            break;
        Py_INCREF(addr_obj);
        len_obj = PyTuple_GetItem(the_buffer_info, 1);
        if (PyErr_Occurred())
            break;
        Py_INCREF(len_obj);
        *addr = PyInt_AsUnsignedLongMask(addr_obj);
        *len = PyInt_AsUnsignedLongMask(len_obj);
        if (PyErr_Occurred())
            break;
      }
    while (false);
    Py_XDECREF(addr_obj);
    Py_XDECREF(len_obj);
    Py_XDECREF(the_buffer_info);
  } /*get_buffer_info*/

static long get_int_property
  (
    PyObject * from_gobject,
    const char * property_name
  )
  /* gets an integer-valued property from a gobject.GObject. */
  {
    long result = 0; /* arbitrary to begin with */
    PyObject * propvalue = 0;
    do /*once*/
      {
        propvalue = PyObject_CallMethod(from_gobject, "get_property", "s", property_name);
        if (propvalue == 0)
            break;
        result = PyInt_AsLong(propvalue);
      }
    while (false);
    Py_XDECREF(propvalue);
    return result;
  } /*get_int_property*/

static bool get_bool_property
  (
    PyObject * from_gobject,
    const char * property_name
  )
  /* gets a boolean-valued property from a gobject.GObject. */
  {
    bool result = false; /* arbitrary to begin with */
    PyObject * propvalue = 0;
    do /*once*/
      {
        propvalue = PyObject_CallMethod(from_gobject, "get_property", "s", property_name);
        if (propvalue == 0)
            break;
        if (!PyBool_Check(propvalue))
          {
            PyErr_SetString
              (
                PyExc_TypeError,
                "a boolean is required"
              );
            break;
          } /*if*/
        result = PyObject_Compare(propvalue, Py_False) != 0;
      }
    while (false);
    Py_XDECREF(propvalue);
    return result;
  } /*get_bool_property*/

static void parse_colors_tuple
  (
    PyObject * color_tuple,
    uint32_t * colors /* array of 4 elements */
  )
  /* parses color_tuple as a tuple of 4 elements, each in turn being a tuple of
    4 integers (r, g, b, a) each in the range [0 .. 255]. Puts the result as
    Cairo-format pixel values into colors. */
  {
    PyObject * the_colors[4] = {0, 0, 0, 0};
    unsigned int nrcolors, i, j, channel[4];
    do /*once*/
      {
        nrcolors = PyTuple_Size(color_tuple);
        if (PyErr_Occurred())
            break;
        if (nrcolors > 4)
          {
            nrcolors = 4;
          } /*if*/
        for (i = 0; i < nrcolors; ++i)
          {
            the_colors[i] = PyTuple_GetItem(color_tuple, i);
            Py_XINCREF(the_colors[i]);
          } /*for*/
        if (PyErr_Occurred())
            break;
        for (i = 0;;)
          {
            if (i == nrcolors)
                break;
            for (j = 0;;)
              {
                if (j == 4)
                    break;
                PyObject * const color_obj = PyTuple_GetItem(the_colors[i], j);
                if (PyErr_Occurred())
                    break;
                const long chanval = PyInt_AsLong(color_obj);
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
        for (; i < 4; ++i)
          {
          /* fill unused entries with transparent colour */
            colors[i] = 0;
          } /*for*/
      }
    while (false);
    for (i = 0; i < 4; ++i)
      {
        Py_XDECREF(the_colors[i]);
      } /*for*/
  } /*parse_colors_tuple*/

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
    unsigned long count_factor;
      /* ignore excess colours provided they make up no more than a proportion
        1 / count_factor of the pixels */
    PyObject * result = 0;
    PyObject * array_module = 0;
    PyObject * srcarray = 0;
    unsigned long pixaddr, nrpixbytes, nrpixels, pixlen;
    const uint32_t * pixels;
    unsigned long nrhistentries = 0, maxhistentries, histindex;
    histentry * histogram = 0;
    PyObject * result_array = 0;
    PyObject * hist_tuple = 0;
    do /*once*/
      {
        array_module = PyImport_ImportModule("array");
        if (array_module == 0)
            break;
        if (!PyArg_ParseTuple(args, "Ok", &srcarray, &count_factor))
            break;
        Py_INCREF(srcarray);
        get_buffer_info(srcarray, &pixaddr, &nrpixbytes);
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
                if (nrhistentries > 0)
                  {
                    histogram[0].index = 0;
                    if (nrhistentries > 1)
                      {
                        histogram[1].index = 1;
                        if (nrhistentries > 2)
                          {
                            histogram[2].index = 2;
                            if (nrhistentries > 3)
                              {
                                histogram[3].index = 3;
                              } /*if*/
                          } /*if*/
                      } /*if*/
                  } /*if*/
                for (histindex = 4; histindex < nrhistentries; ++histindex)
                  {
                  /* coalesce remaining colours to nearest ones among the first four */
                    unsigned long foundindex, bestindex, bestweight;
                    bestweight = 0;
                    for (foundindex = 0; foundindex < 4; ++foundindex)
                      {
                        unsigned int h1, s1, v1, h2, s2, v2;
                        extract_hsv(histogram[histindex].pixel, &h1, &s1, &v1);
                        extract_hsv(histogram[foundindex].pixel, &h2, &s2, &v2);
                        const long
                            delta_a =
                                    (int)(histogram[histindex].pixel >> 24 & 255)
                                -
                                    (int)(histogram[foundindex].pixel >> 24 & 255),
                            delta_h = (int)h1 - (int)h2,
                            delta_s = (int)s1 - (int)s2,
                            delta_v = (int)v1 - (int)v2;
                        const unsigned long weight =
                                delta_a * delta_a
                            +
                                4 * delta_h * delta_h
                                  /* stricter hue matching to reduce colour fringing effects */
                            +
                                delta_s * delta_s
                            +
                                delta_v * delta_v;
                        if (foundindex == 0 || weight < bestweight)
                          {
                            bestindex = foundindex;
                            bestweight = weight;
                          } /*if*/
                      } /*for*/
                    histogram[histindex].index = bestindex;
                  } /*for*/
              }
              {
              /* generate indexed version of image */
                const size_t pixbufsize = 128 /* convenient buffer size to avoid heap allocation */;
                const size_t max_pixels = pixbufsize * 4; /* 2 bits per pixel */
                uint8_t pixbuf[pixbufsize];
                size_t bufpixels;
                result_array = PyObject_CallMethod(array_module, "array", "s", "B");
                if (result_array == 0)
                    break;
                pixels = (const uint32_t *)pixaddr;
                pixlen = nrpixels;
                bufpixels = 0;
                for (;;)
                  {
                  /* extend result_array by a bunch of converted pixels at a time */
                    if (pixlen == 0 || bufpixels == max_pixels)
                      {
                        PyObject * bufstring = 0;
                        PyObject * result = 0;
                        do /*once*/
                          {
                            if (bufpixels == 0)
                                break; /* nothing to flush out */
                            if (bufpixels % 4 != 0)
                              {
                              /* fill out unused part of byte with zeroes--actually shouldn't occur */
                                pixbuf[bufpixels / 4] &= ~(0xff << bufpixels % 4 * 2);
                              } /*if*/
                            bufstring = PyString_FromStringAndSize((const char *)pixbuf, (bufpixels + 3) / 4);
                            if (bufstring == 0)
                                break;
                            result = PyObject_CallMethod(result_array, "fromstring", "O", bufstring);
                            if (result == 0)
                                break;
                          }
                        while (false);
                        Py_XDECREF(result);
                        Py_XDECREF(bufstring);
                        if (PyErr_Occurred())
                            break;
                        if (pixlen == 0)
                            break;
                        bufpixels = 0;
                      } /*if*/
                    const uint32_t thispixel = *pixels;
                    for (histindex = 0; histogram[histindex].pixel != thispixel; ++histindex)
                      /* guaranteed to find pixel index */;
                    if (bufpixels % 4 == 0)
                      {
                      /* starting new byte */
                        pixbuf[bufpixels / 4] = 0; /* ensure there's no junk in it */
                      } /*if*/
                    pixbuf[bufpixels / 4] |= histogram[histindex].index << bufpixels % 4 * 2;
                    ++bufpixels;
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
            result_array = Py_None;
          } /*if*/
        hist_tuple = PyTuple_New(nrhistentries);
        if (hist_tuple == 0)
            break;
        for (histindex = 0;;)
          {
          /* fill in hist_tuple */
            if (histindex == nrhistentries)
                break;
            PyObject * hist_entry_tuple = 0;
            PyObject * color_tuple = 0;
            PyObject * count = 0;
            do /*once*/
              {
                const uint32_t pixel = histogram[histindex].pixel;
                color_tuple = PyTuple_New(4);
                if (color_tuple == 0)
                    break;
                PyTuple_SET_ITEM(color_tuple, 0, PyInt_FromLong(pixel >> 16 & 255)); /* R */
                PyTuple_SET_ITEM(color_tuple, 1, PyInt_FromLong(pixel >> 8 & 255)); /* G */
                PyTuple_SET_ITEM(color_tuple, 2, PyInt_FromLong(pixel & 255)); /* B */
                PyTuple_SET_ITEM(color_tuple, 3, PyInt_FromLong(pixel >> 24 & 255)); /* A */
                count = PyInt_FromLong(histogram[histindex].count);
                hist_entry_tuple = PyTuple_New(2);
                if (PyErr_Occurred())
                    break;
                PyTuple_SET_ITEM(hist_entry_tuple, 0, color_tuple);
                PyTuple_SET_ITEM(hist_entry_tuple, 1, count);
                PyTuple_SET_ITEM(hist_tuple, histindex, hist_entry_tuple);
              /* lose stolen references */
                color_tuple = 0;
                count = 0;
                hist_entry_tuple = 0;
              }
            while (false);
            Py_XDECREF(count);
            Py_XDECREF(color_tuple);
            Py_XDECREF(hist_entry_tuple);
            if (PyErr_Occurred())
                break;
            ++histindex;
          } /*for*/
        if (PyErr_Occurred())
            break;
      /* all done */
        result = Py_BuildValue("OO", result_array, hist_tuple);
      }
    while (false);
    Py_XDECREF(hist_tuple);
    Py_XDECREF(result_array);
    free(histogram);
    Py_XDECREF(srcarray);
    Py_XDECREF(array_module);
    return result;
  } /*spuhelper_index_image*/

static PyObject * spuhelper_expand_image
  (
    PyObject * self,
    PyObject * args
  )
  /* expands a 2-bit-per-pixel image, substituting the specified colours. */
  {
    PyObject * result = 0;
    PyObject * srcarray = 0;
    PyObject * color_tuple = 0;
    PyObject * array_module = 0;
    PyObject * dstarray = 0;
    unsigned long srcpixaddr, nrsrcpixbytes;
    uint32_t colors[4];
    do /*once*/
      {
        if (!PyArg_ParseTuple(args, "OO", &srcarray, &color_tuple))
            break;
        Py_INCREF(srcarray);
        Py_INCREF(color_tuple);
        array_module = PyImport_ImportModule("array");
        if (array_module == 0)
            break;
        get_buffer_info(srcarray, &srcpixaddr, &nrsrcpixbytes);
        if (PyErr_Occurred())
            break;
        parse_colors_tuple(color_tuple, colors);
        if (PyErr_Occurred())
            break;
        dstarray = PyObject_CallMethod(array_module, "array", "s", "B");
        if (dstarray == 0)
            break;
          {
          /* expand the pixels */
            const unsigned long maxbufpixels = 128 /* convenient buffer size to avoid heap allocation */;
            uint32_t pixbuf[maxbufpixels];
            const uint8_t * srcpixels = (const uint8_t *)srcpixaddr;
            unsigned long nr_buf_pixels, nr_src_pixels, src_pix_index;
            unsigned int srcpix;
            nr_src_pixels = nrsrcpixbytes << 2;
            src_pix_index = 0;
            nr_buf_pixels = 0;
            for (;;)
              {
                if (nr_src_pixels == 0 || nr_buf_pixels == maxbufpixels)
                  {
                    PyObject * bufstring = 0;
                    PyObject * result = 0;
                    do /*once*/
                      {
                        if (nr_buf_pixels == 0)
                            break; /* nothing to flush out */
                        bufstring = PyString_FromStringAndSize((const char *)pixbuf, nr_buf_pixels * 4);
                        if (bufstring == 0)
                            break;
                        result = PyObject_CallMethod(dstarray, "fromstring", "O", bufstring);
                        if (result == 0)
                            break;
                      }
                    while (false);
                    Py_XDECREF(result);
                    Py_XDECREF(bufstring);
                    if (PyErr_Occurred())
                        break;
                    if (nr_src_pixels == 0)
                        break;
                    nr_buf_pixels = 0;
                  } /*if*/
                if (src_pix_index == 0)
                  {
                    srcpix = *srcpixels;
                  } /*if*/
                pixbuf[nr_buf_pixels] = colors[srcpix >> src_pix_index * 2 & 3];
                ++nr_buf_pixels;
                ++src_pix_index;
                if (src_pix_index == 4)
                  {
                    ++srcpixels;
                    src_pix_index = 0;
                  } /*if*/
                --nr_src_pixels;
              } /*for*/
            if (PyErr_Occurred())
                break;
          }
      /* all done */
        result = dstarray;
        dstarray = 0; /* so I don't dispose of it yet */
      }
    while (false);
    Py_XDECREF(dstarray);
    Py_XDECREF(array_module);
    Py_XDECREF(color_tuple);
    Py_XDECREF(srcarray);
    return result;
  } /*spuhelper_expand_image*/

static PyObject * spuhelper_gtk_to_cairo_a
  (
    PyObject * self,
    PyObject * args
  )
  /* converts the pixels in an RGB-format pixbuf to Cairo (native-endian) ordering, adding
    a fully-opaque alpha channel, and returns the result in a new array object. */
  {
    PyObject * result = 0;
    PyObject * array_module = 0;
    PyObject * srcpixbuf = 0;
    PyObject * srcpixstring = 0;
    PyObject * srcpixarray = 0;
    PyObject * dstarray = 0;
    unsigned long pixbase, pixlen, image_width, image_height, row_stride;
    unsigned int nr_channels;
    bool has_alpha;
    do /*once*/
      {
        array_module = PyImport_ImportModule("array");
        if (array_module == 0)
            break;
        if (!PyArg_ParseTuple(args, "O", &srcpixbuf))
            break;
        Py_INCREF(srcpixbuf);
        srcpixstring = PyObject_CallMethod(srcpixbuf, "get_pixels", "");
          /* have to make a copy of the pixels, can't get their original address ... sigh */
        if (srcpixstring == 0)
            break;
        srcpixarray = PyObject_CallMethod(array_module, "array", "sO", "B", srcpixstring);
          /* and yet another copy of the pixels, into an array object that I can directly address ... sigh */
        if (srcpixarray == 0)
            break;
        Py_DECREF(srcpixstring);
        srcpixstring = 0; /* try to free up some memory */
        get_buffer_info(srcpixarray, &pixbase, &pixlen);
        if (PyErr_Occurred())
            break;
        image_width = get_int_property(srcpixbuf, "width");
        image_height = get_int_property(srcpixbuf, "height");
        row_stride = get_int_property(srcpixbuf, "rowstride");
        has_alpha = get_bool_property(srcpixbuf, "has-alpha");
        nr_channels = get_int_property(srcpixbuf, "n-channels");
        if (PyErr_Occurred())
            break;
        if (!(has_alpha ? nr_channels == 4 : nr_channels == 3))
          {
            PyErr_SetString
              (
                PyExc_ValueError,
                "image must have 3 components, excluding alpha"
              );
            break;
          } /*if*/
          {
            const size_t max_pixels = 128 /* convenient buffer size to avoid heap allocation */;
            uint32_t pixbuf[max_pixels];
            size_t nr_buf_pixels, cur_row, cols_left;
            const uint8_t * srcpixels;
            unsigned int a, r, g, b;
            dstarray = PyObject_CallMethod(array_module, "array", "s", "B");
            if (dstarray == 0)
                break;
            cur_row = 0;
            cols_left = 0;
            nr_buf_pixels = 0;
            for (;;)
              {
              /* extend dstarray by a bunch of converted pixels at a time */
                if (cols_left == 0 && cur_row < image_height)
                  {
                    srcpixels = (const uint8_t *)(pixbase + cur_row * row_stride);
                    ++cur_row;
                    cols_left = image_width;
                  } /*if*/
                if (cols_left == 0 || nr_buf_pixels == max_pixels)
                  {
                    PyObject * bufstring = 0;
                    PyObject * result = 0;
                    do /*once*/
                      {
                        if (nr_buf_pixels == 0)
                            break; /* nothing to flush out */
                        bufstring = PyString_FromStringAndSize((const char *)pixbuf, nr_buf_pixels * 4);
                        if (bufstring == 0)
                            break;
                        result = PyObject_CallMethod(dstarray, "fromstring", "O", bufstring);
                        if (result == 0)
                            break;
                      }
                    while (false);
                    Py_XDECREF(result);
                    Py_XDECREF(bufstring);
                    if (PyErr_Occurred())
                        break;
                    if (cols_left == 0)
                        break;
                    nr_buf_pixels = 0;
                  } /*if*/
              /* fixme: Cairo wants premultipled alpha, GDK doesn't */
                r = *srcpixels++;
                g = *srcpixels++;
                b = *srcpixels++;
                a = has_alpha ? *srcpixels++ : 255;
                pixbuf[nr_buf_pixels++] =
                        a << 24
                    |
                        r << 16
                    |
                        g << 8
                    |
                        b;
                --cols_left;
              } /*for*/
            if (PyErr_Occurred())
                break;
          }
      /* all done */
        result = dstarray;
        dstarray = 0; /* so I don't dispose of it yet */
      }
    while (false);
    Py_XDECREF(dstarray);
    Py_XDECREF(srcpixarray);
    Py_XDECREF(srcpixstring);
    Py_XDECREF(srcpixbuf);
    Py_XDECREF(array_module);
    return result;
  } /*spuhelper_gtk_to_cairo_a*/  

static PyObject * spuhelper_cairo_to_gtk
  (
    PyObject * self,
    PyObject * args
  )
  /* converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering to
    GTK Pixbuf (big-endian) ordering. */
  {
    PyObject * result = 0;
    PyObject * the_array = 0;
    unsigned long pixaddr, pixlen;
    uint8_t * pixels;
    do /*once*/
      {
        if (!PyArg_ParseTuple(args, "O", &the_array))
            break;
        Py_INCREF(the_array);
        get_buffer_info(the_array, &pixaddr, &pixlen);
        if (PyErr_Occurred())
            break;
        pixels = (uint8_t *)pixaddr;
        pixlen >>= 2;
        while (pixlen > 0)
          {
            const uint32_t thispixel = *(const uint32_t *)pixels;
          /* fixme: Cairo wants premultiplied alpha, GDK doesn't */
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
    Py_XDECREF(the_array);
    return result;
  } /*spuhelper_cairo_to_gtk*/

static PyObject * spuhelper_write_png
  (
    PyObject * self,
    PyObject * args
  )
  /* writes a buffer of two-bit pixels to a PNG file. */
  {
    PyObject * result = 0;
    PyObject * the_array = 0;
    PyObject * color_tuple = 0;
    PyObject * outfile = 0;
    unsigned long pixaddr, pixlen, pixwidth, pixstride, pixheight;
    uint32_t colors[4];
    png_structp pngcontext = 0;
    png_infop pnginfo = 0;

    void outfile_write
      (
        png_structp pngcontext,
        unsigned char * data,
        size_t datasize
      )
      /* PNG data-output callback which passes the data to outfile.write. */
      {
        PyObject * bufstring = 0;
        PyObject * result = 0;
        do /*once*/
          {
            if (PyErr_Occurred())
                break;
            bufstring = PyString_FromStringAndSize((const char *)data, datasize);
            if (bufstring == 0)
                break;
            result = PyObject_CallMethod(outfile, "write", "O", bufstring);
          }
        while (false);
        Py_XDECREF(bufstring);
        Py_XDECREF(result);
      } /*outfile_write*/

    do /*once*/
      {
        if (!PyArg_ParseTuple(args, "OkOO", &the_array, &pixwidth, &color_tuple, &outfile))
            break;
        Py_INCREF(the_array);
        Py_INCREF(color_tuple);
        Py_INCREF(outfile);
        get_buffer_info(the_array, &pixaddr, &pixlen);
        if (PyErr_Occurred())
            break;
        pixstride = (pixwidth + 3) / 4;
        pixheight = pixlen / pixstride;
        parse_colors_tuple(color_tuple, colors);
        if (PyErr_Occurred())
            break;
        pngcontext = png_create_write_struct
          (
            /*user_png_ver =*/ PNG_LIBPNG_VER_STRING,
            /*error_ptr =*/ 0,
            /*error_fn =*/ 0,
            /*warn_fn =*/ 0
          );
        if (pngcontext == 0)
          {
            PyErr_NoMemory();
            break;
          } /*if*/
        pnginfo = png_create_info_struct(pngcontext);
        if (pnginfo == 0)
          {
            PyErr_NoMemory();
            break;
          } /*if*/
        png_set_write_fn
          (
            /*png_ptr =*/ pngcontext,
            /*io_ptr =*/ 0,
            /*write_data_fn =*/ outfile_write,
            /*output_flush_fn =*/ 0
          );
        png_set_IHDR
          (
            /*png_ptr =*/ pngcontext,
            /*info_ptr =*/ pnginfo,
            /*width =*/ pixwidth,
            /*height =*/ pixheight,
            /*bit_depth =*/ 2,
            /*color_type =*/ PNG_COLOR_TYPE_PALETTE,
            /*interlace_method =*/ PNG_INTERLACE_NONE,
            /*compression_method =*/ PNG_COMPRESSION_TYPE_DEFAULT,
            /*filter_method =*/ PNG_FILTER_TYPE_DEFAULT
          );
          {
            png_color pngcolors[4];
            unsigned char alpha[4];
            unsigned int i;
            for (i = 0; i < 4; ++i)
              {
                alpha[i] = colors[i] >> 24 & 255;
                if (alpha[i] != 0)
                  {
                  /* PNG doesn't want premultiplied alpha */
                    pngcolors[i].red = (colors[i] >> 16 & 255) * 255 / alpha[i];
                    pngcolors[i].green = (colors[i] >> 8 & 255) * 255 / alpha[i];
                    pngcolors[i].blue = (colors[i] & 255) * 255 / alpha[i];
                  }
                else
                  {
                    pngcolors[i].red = 0;
                    pngcolors[i].green = 0;
                    pngcolors[i].blue = 0;
                  } /*if*/
              } /*for*/
            png_set_PLTE
              (
                /*png_ptr =*/ pngcontext,
                /*info_ptr =*/ pnginfo,
                /*palette =*/ pngcolors,
                /*num_palette =*/ 4
              );
            png_set_tRNS
              (
                /*png_ptr =*/ pngcontext,
                /*info_ptr =*/ pnginfo,
                /*trans =*/ alpha,
                /*num_trans =*/ 4,
                /*trans_values =*/ 0
              );
          }
        png_write_info(pngcontext, pnginfo);
        png_set_packswap(pngcontext);
          {
            unsigned long row;
            for (row = 0;;)
              {
                if (row == pixheight)
                    break;
                png_write_row
                  (
                    /*png_ptr =*/ pngcontext,
                    /*row =*/ (png_bytep)(pixaddr + pixstride * row)
                  );
                if (PyErr_Occurred())
                    break;
                ++row;
              } /*for*/
            if (PyErr_Occurred())
                break;
            png_write_end(pngcontext, pnginfo);
          }
        if (PyErr_Occurred())
            break;
      /* all done */
        Py_INCREF(Py_None);
        result = Py_None;
      }
    while (false);
    if (pngcontext != 0)
      {
        png_destroy_write_struct(&pngcontext, &pnginfo);
      } /*if*/
    Py_XDECREF(outfile);
    Py_XDECREF(color_tuple);
    Py_XDECREF(the_array);
    return result;
  } /*spuhelper_write_png*/

static PyObject * spuhelper_read_png_palette
  (
    PyObject * self,
    PyObject * args
  )
  /* loads the palette definition, if any, from a PNG file. */
  {
    PyObject * result = 0;
    PyObject * result_colors = 0;
    PyObject * infile = 0;
    png_structp pngcontext = 0;
    png_infop pnginfo = 0;
    int bit_depth, color_type;
    png_color * pngcolors;
    unsigned char * alpha;
    int nrcolors, nrtransparent;
    unsigned int i;
    PyObject * color_tuple = 0;

    void infile_read
      (
        png_structp pngcontext,
        unsigned char * data,
        size_t datasize
      )
      /* PNG data-output callback which obtains data from infile.read. */
      {
        PyObject * bufstring = 0;
        const unsigned char * chars;
        Py_ssize_t nr_chars;
        do /*once*/
          {
            if (PyErr_Occurred())
                break;
            bufstring = PyObject_CallMethod(infile, "read", "k", datasize);
            if (PyErr_Occurred())
                break;
            PyString_AsStringAndSize(bufstring, (char **)&chars, &nr_chars);
            if (PyErr_Occurred())
                break;
            if (nr_chars < datasize)
              {
                PyErr_SetString
                  (
                    PyExc_RuntimeError,
                    "Premature EOF encountered in input PNG file"
                  );
              /* break; */ /* continue to return what I can */
              } /*if*/
            memcpy(data, chars, nr_chars);
          }
        while (false);
        Py_XDECREF(bufstring);
      } /*infile_read*/

    do /*once*/
      {
        if (!PyArg_ParseTuple(args, "O", &infile))
            break;
        Py_INCREF(infile);
        pngcontext = png_create_read_struct
          (
            /*user_png_ver =*/ PNG_LIBPNG_VER_STRING,
            /*error_ptr =*/ 0,
            /*error_fn =*/ 0,
            /*warn_fn =*/ 0
          );
        if (pngcontext == 0)
          {
            PyErr_NoMemory();
            break;
          } /*if*/
        if (setjmp(png_jmpbuf(pngcontext)))
          {
            PyErr_SetString
              (
                PyExc_RuntimeError,
                "PNG error"
              );
            break;
          } /*if*/
        pnginfo = png_create_info_struct(pngcontext);
        if (pnginfo == 0)
          {
            PyErr_NoMemory();
            break;
          } /*if*/
        png_set_read_fn
          (
            /*png_ptr =*/ pngcontext,
            /*io_ptr =*/ 0,
            /*read_data_fn =*/ infile_read
          );
        png_read_info(pngcontext, pnginfo);
#if 1
        bit_depth = png_get_bit_depth(pngcontext, pnginfo);
        color_type = png_get_color_type(pngcontext, pnginfo);
#else /* why doesn't this work? */
        (void)png_get_IHDR
          (
            /*png_ptr =*/ pngcontext,
            /*info_ptr =*/ pnginfo,
            /*width =*/ 0,
            /*height =*/ 0,
            /*bit_depth =*/ &bit_depth,
            /*color_type =*/ &color_type,
            /*interlace_method =*/ 0,
            /*compression_method =*/ 0,
            /*filter_method =*/ 0
          );
#endif
        if (color_type != PNG_COLOR_TYPE_PALETTE)
          {
          /* no palette to return */
            Py_INCREF(Py_None);
            result = Py_None;
            break;
          } /*if*/
        (void)png_get_PLTE /* not expecting this to fail */
          (
            /*png_ptr =*/ pngcontext,
            /*info_ptr =*/ pnginfo,
            /*palette =*/ &pngcolors,
            /*num_palette =*/ &nrcolors
          );
        if
          (
                png_get_tRNS
                  (
                    /*png_ptr =*/ pngcontext,
                    /*info_ptr =*/ pnginfo,
                    /*trans =*/ &alpha,
                    /*num_trans =*/ &nrtransparent,
                    /*trans_values =*/ 0
                  )
            ==
                0
          )
          {
            nrtransparent = 0;
          } /*if*/
        result_colors = PyTuple_New(nrcolors);
        if (result_colors == 0)
            break;
        i = 0;
        for (;;)
          {
            if (i == nrcolors)
                break;
            color_tuple = PyTuple_New(4);
            if (color_tuple == 0)
                break;
          /* convert colours to premultiplied alpha */
            PyTuple_SET_ITEM
              (
                color_tuple,
                0,
                PyInt_FromLong(pngcolors[i].red * (i < nrtransparent ? 255 : alpha[i]) / 255)
              ); /* R */
            PyTuple_SET_ITEM
              (
                color_tuple,
                1,
                PyInt_FromLong(pngcolors[i].green * (i < nrtransparent ? 255 : alpha[i]) / 255)
              ); /* G */
            PyTuple_SET_ITEM
              (
                color_tuple,
                2,
                PyInt_FromLong(pngcolors[i].blue * (i < nrtransparent ? 255 : alpha[i]) / 255)
              ); /* B */
            PyTuple_SET_ITEM(color_tuple, 3, PyInt_FromLong(i < nrtransparent ? alpha[i] : 255)); /* A */
            if (PyErr_Occurred())
                break;
            PyTuple_SET_ITEM(result_colors, i, color_tuple);
            color_tuple = 0; /* lose stolen reference */
            ++i;
          } /*for*/
        if (PyErr_Occurred())
            break;
      /* all done */
        result = result_colors;
        result_colors = 0; /* so I don't dispose of it yet */
      }
    while (false);
    if (pngcontext != 0)
      {
        png_destroy_read_struct(&pngcontext, &pnginfo, 0);
      } /*if*/
    Py_XDECREF(color_tuple);
    Py_XDECREF(result_colors);
    Py_XDECREF(infile);
    return result;
  } /*spuhelper_read_png_palette*/

static PyMethodDef spuhelper_methods[] =
  {
    {"index_image", spuhelper_index_image, METH_VARARGS,
        "index_image(array, count_factor)\n"
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
    {"gtk_to_cairo_a", spuhelper_gtk_to_cairo_a, METH_VARARGS,
        "gtk_to_cairo_a(pixbuf)\n"
            "converts the pixels in an RGB-format pixbuf to Cairo (native-endian)"
            " ordering, adding a fully-opaque alpha channel, and returns the result"
            " in a new array object."
    },
    {"cairo_to_gtk", spuhelper_cairo_to_gtk, METH_VARARGS,
        "cairo_to_gtk(array)\n"
        "converts a buffer of RGBA-format pixels from Cairo (native-endian) ordering"
        " to GTK Pixbuf (big-endian) ordering."
    },
    {"write_png", spuhelper_write_png, METH_VARARGS,
        "write_png(array, width, colors, outfile)\n"
        "writes a buffer of two-bit pixels, as previously generated by index_image,"
            " in PNG format to outfile."
    },
    {"read_png_palette", spuhelper_read_png_palette, METH_VARARGS,
        "read_png_palette(infile)\n"
        "returns the palette from the PNG file as a tuple, if it has one."
    },
    {0, 0, 0, 0} /* marks end of list */
  };

PyMODINIT_FUNC initspuhelper(void)
  {
    (void)Py_InitModule3("spuhelper", spuhelper_methods,
        "helper functions for dvd_menu_animator script");
  } /*initspuhelper*/
