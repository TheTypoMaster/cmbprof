/* DomHTMLAreaElement.java -- 
   Copyright (C) 2005 Free Software Foundation, Inc.

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

package gnu.xml.dom.html2;

import org.w3c.dom.html2.HTMLAreaElement;

/**
 * An HTML 'AREA' element node.
 *
 * @author <a href='mailto:dog@gnu.org'>Chris Burdess</a>
 */
public class DomHTMLAreaElement
  extends DomHTMLElement
  implements HTMLAreaElement
{

  protected DomHTMLAreaElement(DomHTMLDocument owner, String namespaceURI,
                               String name)
  {
    super(owner, namespaceURI, name);
  }

  public String getAccessKey()
  {
    return getHTMLAttribute("accesskey");
  }

  public void setAccessKey(String accessKey)
  {
    setHTMLAttribute("accesskey", accessKey);
  }
  
  public String getAlt()
  {
    return getHTMLAttribute("alt");
  }

  public void setAlt(String alt)
  {
    setHTMLAttribute("alt", alt);
  }
  
  public String getCoords()
  {
    return getHTMLAttribute("coords");
  }

  public void setCoords(String coords)
  {
    setHTMLAttribute("coords", coords);
  }
  
  public String getHref()
  {
    return getHTMLAttribute("href");
  }

  public void setHref(String href)
  {
    setHTMLAttribute("href", href);
  }
  
  public boolean getNoHref()
  {
    return getBooleanHTMLAttribute("nohref");
  }

  public void setNoHref(boolean nohref)
  {
    setBooleanHTMLAttribute("nohref", nohref);
  }
  
  public String getShape()
  {
    return getHTMLAttribute("shape");
  }

  public void setShape(String shape)
  {
    setHTMLAttribute("shape", shape);
  }
  
  public int getTabIndex()
  {
    return getIntHTMLAttribute("tabindex");
  }

  public void setTabIndex(int tabIndex)
  {
    setIntHTMLAttribute("tabindex", tabIndex);
  }
  
  public String getTarget()
  {
    return getHTMLAttribute("target");
  }

  public void setTarget(String target)
  {
    setHTMLAttribute("target", target);
  }
  
}

