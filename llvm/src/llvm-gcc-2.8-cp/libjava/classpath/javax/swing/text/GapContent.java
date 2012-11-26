/* GapContent.java --
   Copyright (C) 2002, 2004, 2005, 2006 Free Software Foundation, Inc.

This file is part of GNU Classpath.

GNU Classpath is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2, or (at your option)
any later version.

GNU Classpath is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with GNU Classpath; see the file COPYING.  If not, write to the
Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
02110-1301 USA.

Linking this library statically or dynamically with other modules is
making a combined work based on this library.  Thus, the terms and
conditions of the GNU General Public License cover the whole
combination.

As a special exception, the copyright holders of this library give you
permission to link this library with independent modules to produce an
executable, regardless of the license terms of these independent
modules, and to copy and distribute the resulting executable under
terms of your choice, provided that you also meet, for each linked
independent module, the terms and conditions of the license of that
module.  An independent module is a module which is not derived from
or based on this library.  If you modify this library, you may extend
this exception to your version of the library, but you are not
obligated to do so.  If you do not wish to do so, delete this
exception statement from your version. */


package javax.swing.text;

import java.io.Serializable;
import java.lang.ref.Reference;
import java.lang.ref.ReferenceQueue;
import java.lang.ref.WeakReference;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Iterator;
import java.util.List;
import java.util.Set;
import java.util.Vector;
import java.util.WeakHashMap;

import javax.swing.undo.AbstractUndoableEdit;
import javax.swing.undo.CannotRedoException;
import javax.swing.undo.CannotUndoException;
import javax.swing.undo.UndoableEdit;

/**
 * This implementation of {@link AbstractDocument.Content} uses a gapped buffer.
 * This takes advantage of the fact that text area content is mostly inserted
 * sequentially. The buffer is a char array that maintains a gap at the current
 * insertion point. If characters a inserted at gap boundaries, the cost is
 * minimal (simple array access). The array only has to be shifted around when
 * the insertion point moves (then the gap also moves and one array copy is
 * necessary) or when the gap is filled up and the buffer has to be enlarged.
 */
public class GapContent
    implements AbstractDocument.Content, Serializable
{
  
  /**
   * A {@link Position} implementation for <code>GapContent</code>.
   */
  private class GapContentPosition
    implements Position
  {

    /**
     * The index to the positionMarks array entry, which in turn holds the
     * mark into the buffer array.
     */
    Mark mark;

    /**
     * Creates a new GapContentPosition object.
     * 
     * @param offset the offset of this Position
     */
    GapContentPosition(int offset)
    {
      // Try to find the mark in the positionMarks array, and store the index
      // to it.
      synchronized (GapContent.this)
        {
          // Try to make space.
          garbageCollect();
          Mark m = new Mark(offset);
          int i = search(marks, m);
          if (i >= 0) // mark found
            {
              m = (Mark) marks.get(i);
            }
          else
            {
              i = -i - 1;
              marks.add(i, m);
            }
          m.refCount++;
          mark = m;
        }

      // Register this position in the death queue, so we can cleanup the marks
      // when this position object gets GC'ed.
      new WeakReference(this, queueOfDeath);
    }

    /**
     * Returns the current offset of this Position within the content.
     * 
     * @return the current offset of this Position within the content.
     */
    public int getOffset()
    {
      return mark.getOffset();
    }
  }

  /**
   * Holds a mark into the buffer that is used by GapContentPosition to find
   * the actual offset of the position. This is pulled out of the
   * GapContentPosition object so that the mark and position can be handled
   * independently, and most important, so that the GapContentPosition can
   * be garbage collected while we still hold a reference to the Mark object. 
   */
  private class Mark
    implements Comparable
  {
    /**
     * The actual mark into the buffer.
     */
    int mark;

    /**
     * The number of GapContentPosition object that reference this mark. If
     * it reaches zero, it get's deleted by {@link GapContent#garbageCollect()}.
     */
    int refCount;

    /**
     * Creates a new Mark object for the specified offset.
     *
     * @param offset the offset
     */
    Mark(int offset)
    {
      mark = offset;
      if (mark >= gapStart && mark != 0)
        mark += (gapEnd - gapStart);
    }

    /**
     * Returns the offset of the mark.
     *
     * @return the offset of the mark
     */
    int getOffset()
    {
      assert mark == 0 || mark < gapStart || mark >= gapEnd :
             "Invalid mark: " + mark + ", gapStart: " + gapStart
             + ", gapEnd: " + gapEnd;

      int res = mark;
      if (mark >= gapEnd)
        res -= (gapEnd - gapStart);
      return res;
    }

    /**
     * Implementation of Comparable.
     */
    public int compareTo(Object o)
    {
      Mark other = (Mark) o;
      return mark - other.mark;
    }
    /**
     * Adjustment for equals().
     */
    public boolean equals(Object o)
    {
      if (o == null || !(o instanceof Mark))
        return false;
      else
        return ((Mark) o).mark == mark;
    }
  }

  private class InsertUndo extends AbstractUndoableEdit
  {
    public int where, length;
    String text;
    public InsertUndo(int start, int len)
    {
      where = start;
      length = len;
    }

    public void undo () throws CannotUndoException
    {
      super.undo();
      try
      {
        text = getString(where, length);
        remove(where, length);
      }
      catch (BadLocationException ble)
      {
        throw new CannotUndoException();
      }
    }
    
    public void redo () throws CannotUndoException
    {
      super.redo();
      try
      {
        insertString(where, text);
      }
      catch (BadLocationException ble)
      {
        throw new CannotRedoException();
      }
    }
    
  }
  
  private class UndoRemove extends AbstractUndoableEdit
  {
    public int where;
    String text;
    public UndoRemove(int start, String removedText)
    {
      where = start;
      text = removedText;
    }

    public void undo () throws CannotUndoException
    {
      super.undo();
      try
      {
        insertString(where, text);
      }
      catch (BadLocationException ble)
      {
        throw new CannotUndoException();
      }
    }
    
    public void redo () throws CannotUndoException
    {
      super.redo();
      try
      {
        remove(where, text.length());
      }
      catch (BadLocationException ble)
      {
        throw new CannotRedoException();
      }
    }
    
  }

  /** The serialization UID (compatible with JDK1.5). */
  private static final long serialVersionUID = -6226052713477823730L;

  /**
   * This is the default buffer size and the amount of bytes that a buffer is
   * extended if it is full.
   */
  static final int DEFAULT_BUFSIZE = 10;

  /**
   * The text buffer.
   */
  char[] buffer;

  /**
   * The index of the first character of the gap.
   */
  int gapStart;

  /**
   * The index of the character after the last character of the gap.
   */
  int gapEnd;

  // FIXME: We might want to track GC'ed GapContentPositions and remove their
  // corresponding marks, or alternativly, perform some regular cleanup of
  // the positionMarks array.

  /**
   * Holds the marks for positions. These marks are referenced by the
   * GapContentPosition instances by an index into this array.
   *
   * This is package private to avoid accessor synthetic methods.
   */
  ArrayList marks;

  WeakHashMap positions;

  /**
   * Queues all references to GapContentPositions that are about to be
   * GC'ed. This is used to remove the corresponding marks from the
   * positionMarks array if the number of references to that mark reaches zero.
   *
   * This is package private to avoid accessor synthetic methods.
   */
  ReferenceQueue queueOfDeath;

  /**
   * Creates a new GapContent object.
   */
  public GapContent()
  {
    this(DEFAULT_BUFSIZE);
  }

  /**
   * Creates a new GapContent object with a specified initial size.
   * 
   * @param size the initial size of the buffer
   */
  public GapContent(int size)
  {
    size = Math.max(size, 2);
    buffer = (char[]) allocateArray(size);
    gapStart = 1;
    gapEnd = size;
    buffer[0] = '\n';
    positions = new WeakHashMap();
    marks = new ArrayList();
    queueOfDeath = new ReferenceQueue();
  }

  /**
   * Allocates an array of the specified length that can then be used as
   * buffer.
   * 
   * @param size the size of the array to be allocated
   * 
   * @return the allocated array
   */
  protected Object allocateArray(int size)
  {
    return new char[size];
  }

  /**
   * Returns the length of the allocated buffer array.
   * 
   * @return the length of the allocated buffer array
   */
  protected int getArrayLength()
  {
    return buffer.length;
  }

  /**
   * Returns the length of the content.
   * 
   * @return the length of the content
   */
  public int length()
  {
    return buffer.length - (gapEnd - gapStart);
  }

  /**
   * Inserts a string at the specified position.
   * 
   * @param where the position where the string is inserted
   * @param str the string that is to be inserted
   * 
   * @return an UndoableEdit object
   * 
   * @throws BadLocationException if <code>where</code> is not a valid
   *         location in the buffer
   */
  public UndoableEdit insertString(int where, String str)
      throws BadLocationException
  {
    // check arguments
    int length = length();
    int strLen = str.length();

    if (where < 0)
      throw new BadLocationException("The where argument cannot be smaller"
                                     + " than the zero", where);

    if (where > length)
      throw new BadLocationException("The where argument cannot be greater"
          + " than the content length", where);

    replace(where, 0, str.toCharArray(), strLen);

    return new InsertUndo(where, strLen);
  }

  /**
   * Removes a piece of content at th specified position.
   * 
   * @param where the position where the content is to be removed
   * @param nitems number of characters to be removed
   * 
   * @return an UndoableEdit object
   * 
   * @throws BadLocationException if <code>where</code> is not a valid
   *         location in the buffer
   */
  public UndoableEdit remove(int where, int nitems) throws BadLocationException
  {
    // check arguments
    int length = length();
    
    if ((where + nitems) >= length)
      throw new BadLocationException("where + nitems cannot be greater"
          + " than the content length", where + nitems);
    
    String removedText = getString(where, nitems);
    replace(where, nitems, null, 0);

    return new UndoRemove(where, removedText);
  }

  /**
   * Returns a piece of content as String.
   * 
   * @param where the start location of the fragment
   * @param len the length of the fragment
   * 
   * @throws BadLocationException if <code>where</code> or
   *         <code>where + len</code> are no valid locations in the buffer
   */
  public String getString(int where, int len) throws BadLocationException
  {
    Segment seg = new Segment();
    try
      {
        getChars(where, len, seg);
        return new String(seg.array, seg.offset, seg.count);
      }
    catch (StringIndexOutOfBoundsException ex)
      {
        int invalid = 0;
        if (seg.offset < 0 || seg.offset >= seg.array.length)
          invalid = seg.offset;
        else
          invalid = seg.offset + seg.count;
        throw new BadLocationException("Illegal location: array.length = "
                                       + seg.array.length + ", offset = "
                                       + seg.offset + ", count = "
                                       + seg.count, invalid);
      }
  }

  /**
   * Fetches a piece of content and stores it in a {@link Segment} object.
   * 
   * If the requested piece of text spans the gap, the content is copied into a
   * new array. If it doesn't then it is contiguous and the actual content
   * store is returned.
   * 
   * @param where the start location of the fragment
   * @param len the length of the fragment
   * @param txt the Segment object to store the fragment in
   * 
   * @throws BadLocationException if <code>where</code> or
   *         <code>where + len</code> are no valid locations in the buffer
   */
  public void getChars(int where, int len, Segment txt)
      throws BadLocationException
  {
    // check arguments
    int length = length();
    if (where < 0)
      throw new BadLocationException("the where argument may not be below zero", where);
    if (where >= length)
      throw new BadLocationException("the where argument cannot be greater"
          + " than the content length", where);
    if ((where + len) > length)
      throw new BadLocationException("len plus where cannot be greater"
          + " than the content length", len + where);
    if (len < 0)
      throw new BadLocationException("negative length not allowed: ", len);

    // check if requested segment is contiguous
    if ((where < gapStart) && ((gapStart - where) < len))
    {
      // requested segment is not contiguous -> copy the pieces together
      char[] copy = new char[len];
      int lenFirst = gapStart - where; // the length of the first segment
      System.arraycopy(buffer, where, copy, 0, lenFirst);
      System.arraycopy(buffer, gapEnd, copy, lenFirst, len - lenFirst);
      txt.array = copy;
      txt.offset = 0;
      txt.count = len;
    }
    else
    {
      // requested segment is contiguous -> we can simply return the
      // actual content
      txt.array = buffer;
      if (where < gapStart)
        txt.offset = where;
      else
        txt.offset = where + (gapEnd - gapStart);
      txt.count = len;
    }
  }

  /**
   * Creates and returns a mark at the specified position.
   * 
   * @param offset the position at which to create the mark
   * 
   * @return the create Position object for the mark
   * 
   * @throws BadLocationException if the offset is not a valid position in the
   *         buffer
   */
  public Position createPosition(final int offset) throws BadLocationException
  {
    // Implementation note: We used to perform explicit check on the offset
    // here. However, this makes some Mauve and Intel/Harmony tests fail
    // and luckily enough the GapContent can very well deal with offsets
    // outside the buffer bounds. So I removed that check.

    // We try to find a GapContentPosition at the specified offset and return
    // that. Otherwise we must create a new one.
    GapContentPosition pos = null;
    Set positionSet = positions.keySet();
    for (Iterator i = positionSet.iterator(); i.hasNext();)
      {
        GapContentPosition p = (GapContentPosition) i.next();
        if (p.getOffset() == offset)
          {
            pos = p;
            break;
          }
      }

    // If none was found, then create and return a new one.
    if (pos == null)
      {
        pos = new GapContentPosition(offset);
        positions.put(pos, null);
      }

    return pos;
  }

  /**
   * Enlarges the gap. This allocates a new bigger buffer array, copy the
   * segment before the gap as it is and the segment after the gap at the end
   * of the new buffer array. This does change the gapEnd mark but not the
   * gapStart mark.
   * 
   * @param newSize the new size of the gap
   */
  protected void shiftEnd(int newSize)
  {
    assert newSize > (gapEnd - gapStart) : "The new gap size must be greater "
                                           + "than the old gap size";

    int delta = newSize - gapEnd + gapStart;
    // Update the marks after the gapEnd.
    adjustPositionsInRange(gapEnd, -1, delta);

    // Copy the data around.
    char[] newBuf = (char[]) allocateArray(length() + newSize);
    System.arraycopy(buffer, 0, newBuf, 0, gapStart);
    System.arraycopy(buffer, gapEnd, newBuf, gapStart + newSize, buffer.length
        - gapEnd);
    gapEnd = gapStart + newSize;
    buffer = newBuf;

  }

  /**
   * Shifts the gap to the specified position.
   * 
   * @param newGapStart the new start position of the gap
   */
  protected void shiftGap(int newGapStart)
  {
    if (newGapStart == gapStart)
      return;
    int newGapEnd = newGapStart + gapEnd - gapStart;
    if (newGapStart < gapStart)
      {
        // Update the positions between newGapStart and (old) gapStart. The marks
        // must be shifted by (gapEnd - gapStart).
        adjustPositionsInRange(newGapStart, gapStart, gapEnd - gapStart);
        System.arraycopy(buffer, newGapStart, buffer, newGapEnd, gapStart
                         - newGapStart);
        gapStart = newGapStart;
        gapEnd = newGapEnd;
      }
    else
      {
        // Update the positions between newGapEnd and (old) gapEnd. The marks
        // must be shifted by (gapEnd - gapStart).
        adjustPositionsInRange(gapEnd, newGapEnd, -(gapEnd - gapStart));
        System.arraycopy(buffer, gapEnd, buffer, gapStart, newGapStart
                         - gapStart);
        gapStart = newGapStart;
        gapEnd = newGapEnd;
      }
    resetMarksAtZero();
  }

  /**
   * Shifts the gap start downwards. This does not affect the content of the
   * buffer. This only updates the gap start and all the marks that are between
   * the old gap start and the new gap start. They all are squeezed to the start
   * of the gap, because their location has been removed.
   *
   * @param newGapStart the new gap start
   */
  protected void shiftGapStartDown(int newGapStart)
  {
    if (newGapStart == gapStart)
      return;

    assert newGapStart < gapStart : "The new gap start must be less than the "
                                    + "old gap start.";
    setPositionsInRange(newGapStart, gapStart, false);
    gapStart = newGapStart;
    resetMarksAtZero();
  }

  /**
   * Shifts the gap end upwards. This does not affect the content of the
   * buffer. This only updates the gap end and all the marks that are between
   * the old gap end and the new end start. They all are squeezed to the end
   * of the gap, because their location has been removed.
   *
   * @param newGapEnd the new gap start
   */
  protected void shiftGapEndUp(int newGapEnd)
  {
    if (newGapEnd == gapEnd)
      return;

    assert newGapEnd > gapEnd : "The new gap end must be greater than the "
                                + "old gap end.";
    setPositionsInRange(gapEnd, newGapEnd, false);
    gapEnd = newGapEnd;
    resetMarksAtZero();
  }

  /**
   * Returns the allocated buffer array.
   * 
   * @return the allocated buffer array
   */
  protected final Object getArray()
  {
    return buffer;
  }

  /**
   * Replaces a portion of the storage with the specified items.
   * 
   * @param position the position at which to remove items
   * @param rmSize the number of items to remove
   * @param addItems the items to add at location
   * @param addSize the number of items to add
   */
  protected void replace(int position, int rmSize, Object addItems,
                         int addSize)
  {
    if (gapStart != position)
      shiftGap(position);
      
    // Remove content
    if (rmSize > 0) 
      shiftGapEndUp(gapEnd + rmSize);

    // If gap is too small, enlarge the gap.
    if ((gapEnd - gapStart) <= addSize)
      shiftEnd((addSize - gapEnd + gapStart + 1) * 2 + gapEnd + DEFAULT_BUFSIZE);

    // Add new items to the buffer.
    if (addItems != null)
      {
        System.arraycopy(addItems, 0, buffer, gapStart, addSize);
        gapStart += addSize;
      }
  }

  /**
   * Returns the start index of the gap within the buffer array.
   *
   * @return the start index of the gap within the buffer array
   */
  protected final int getGapStart()
  {
    return gapStart;
  }

  /**
   * Returns the end index of the gap within the buffer array.
   *
   * @return the end index of the gap within the buffer array
   */
  protected final int getGapEnd()
  {
    return gapEnd;
  }

  /**
   * Returns all <code>Position</code>s that are in the range specified by
   * <code>offset</code> and </code>length</code> within the buffer array.
   *
   * @param v the vector to use; if <code>null</code>, a new Vector is allocated
   * @param offset the start offset of the range to search
   * @param length the length of the range to search
   *
   * @return the positions within the specified range
   */
  protected Vector getPositionsInRange(Vector v, int offset, int length)
  {
    Vector res = v;
    if (res == null)
      res = new Vector();
    else
      res.clear();

    int endOffs = offset + length;

    Set positionSet = positions.keySet();
    for (Iterator i = positionSet.iterator(); i.hasNext();)
      {
        GapContentPosition p = (GapContentPosition) i.next();
        int offs = p.getOffset();
        if (offs >= offset && offs < endOffs)
          res.add(p);
      }

    return res;
  }
  
  /**
   * Crunches all positions in the specified range to either the start or
   * end of that interval. The interval boundaries are meant to be inclusive
   * [start, end].
   *
   * @param start the start offset of the range
   * @param end the end offset of the range
   * @param toStart a boolean indicating if the positions should be crunched
   *        to the start (true) or to the end (false)
   */
  private void setPositionsInRange(int start, int end, boolean toStart)
  {
    synchronized (this)
      {
        // Find the start and end indices in the positionMarks array.
        Mark m = new Mark(0); // For comparison / search only.
        m.mark = start;
        int startIndex = search(marks, m);
        if (startIndex < 0) // Translate to insertion index, if not found.
          startIndex = - startIndex - 1;
        m.mark = end;
        int endIndex = search(marks, m);
        if (endIndex < 0) // Translate to insertion index - 1, if not found.
          endIndex = - endIndex - 2;

        // Actually adjust the marks.
        for (int i = startIndex; i <= endIndex; i++)
          ((Mark) marks.get(i)).mark = toStart ? start : end;
      }

  }

  /**
   * Adjusts the mark of all <code>Position</code>s that are in the range 
   * specified by <code>offset</code> and </code>length</code> within 
   * the buffer array by <code>increment</code>
   *
   * @param startOffs the start offset of the range to search
   * @param endOffs the length of the range to search, -1 means all to the end
   * @param incr the increment
   */
  private void adjustPositionsInRange(int startOffs, int endOffs, int incr)
  {
    synchronized (this)
      {
        // Find the start and end indices in the positionMarks array.
        Mark m = new Mark(0); // For comparison / search only.

        m.mark = startOffs;
        int startIndex = search(marks, m);
        if (startIndex < 0) // Translate to insertion index, if not found.
          startIndex = - startIndex - 1;

        m.mark = endOffs;
        int endIndex;
        if (endOffs == -1)
          endIndex = marks.size() - 1;
        else
          {
            endIndex = search(marks, m);
            if (endIndex < 0) // Translate to insertion index - 1, if not found.
              endIndex = - endIndex - 2;
          }
        // Actually adjust the marks.
        for (int i = startIndex; i <= endIndex; i++) {
          ((Mark) marks.get(i)).mark += incr;
        }
      }

  }

  /**
   * Resets all <code>Position</code> that have an offset of <code>0</code>,
   * to also have an array index of <code>0</code>. This might be necessary
   * after a call to <code>shiftGap(0)</code>, since then the marks at offset
   * <code>0</code> get shifted to <code>gapEnd</code>.
   */
  protected void resetMarksAtZero()
  {
    if (gapStart != 0)
      return;

    for (int i = 0; i < marks.size(); i++)
      {
        Mark m = (Mark) marks.get(i);
        if (m.mark <= gapEnd)
          m.mark = 0;
      }
  }

  /**
   * @specnote This method is not very well specified and the positions vector
   *           is implementation specific. The undo positions are managed
   *           differently in this implementation, this method is only here
   *           for binary compatibility.
   */
  protected void updateUndoPositions(Vector positions, int offset, int length)
  {
    // We do nothing here.
  }

  /**
   * Outputs debugging info to System.err. It prints out the buffer array,
   * the gapStart is marked by a &lt; sign, the gapEnd is marked by a &gt;
   * sign and each position is marked by a # sign.
   */
  private void dump()
  {
    System.err.println("GapContent debug information");
    System.err.println("buffer length: " + buffer.length);
    System.err.println("gap start: " + gapStart);
    System.err.println("gap end: " + gapEnd);
    for (int i = 0; i < buffer.length; i++)
      {
        if (i == gapStart)
          System.err.print('<');
        if (i == gapEnd)
          System.err.print('>');

        if (!Character.isISOControl(buffer[i]))
          System.err.print(buffer[i]);
        else
          System.err.print('.');
      }
    System.err.println();
  }

  /**
   * Prints out the position marks.
   */
  private void dumpMarks()
  {
    System.out.print("positionMarks: ");
    for (int i = 0; i < marks.size(); i++)
      System.out.print(((Mark) marks.get(i)).mark + ", ");
    System.out.println();
  }

  /**
   * Polls the queue of death for GapContentPositions, updates the
   * corresponding reference count and removes the corresponding mark
   * if the refcount reaches zero.
   *
   * This is package private to avoid accessor synthetic methods.
   */
  void garbageCollect()
  {
    Reference ref = queueOfDeath.poll();
    while (ref != null)
      {
        if (ref != null)
          {
            GapContentPosition pos = (GapContentPosition) ref.get();
            Mark m = pos.mark;
            m.refCount--;
            if (m.refCount == 0)
              marks.remove(m);
          }
        ref = queueOfDeath.poll();
      }
  }

  /**
   * Searches the first occurance of object <code>o</code> in list
   * <code>l</code>. This performs a binary search by calling
   * {@link Collections#binarySearch(List, Object)} and when an object has been
   * found, it searches backwards to the first occurance of that object in the
   * list. The meaning of the return value is the same as in
   * <code>Collections.binarySearch()</code>.
   *
   * @param l the list to search through
   * @param o the object to be searched
   *
   * @return the index of the first occurance of o in l, or -i + 1 if not found
   */
  private int search(List l, Object o)
  {
    int i = Collections.binarySearch(l, o);
    while (i > 0)
      {
        Object o2 = l.get(i - 1);
        if (o2.equals(o))
          i--;
        else
          break;
      }
    return i;
  }
}