// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#include "dbBlock.h"
#include "dbDatabase.h"
#include "dbNet.h"
#include "dbTable.h"
#include "dbWire.h"
#include "dbWireOpcode.h"
#include "odb/db.h"
#include "odb/dbShape.h"
#include "odb/dbWireCodec.h"
#include "utl/Logger.h"

namespace odb {

#define DB_WIRE_DECODE_INVALID_OPCODE 0

//////////////////////////////////////////////////////////////////////////////////
//
// dbWirePathItr
//
//////////////////////////////////////////////////////////////////////////////////
dbWirePathItr::dbWirePathItr()
{
  _opcode = dbWireDecoder::END_DECODE;
  _prev_x = 0;
  _prev_y = 0;
  _prev_ext = 0;
  _has_prev_ext = false;
  _dw = 0;
  _rule = nullptr;
  _wire = nullptr;
}

dbWirePathItr::~dbWirePathItr()
{
}

void dbWirePathItr::begin(dbWire* wire)
{
  _decoder.begin(wire);
  _opcode = _decoder.next();
  _prev_x = 0;
  _prev_y = 0;
  _prev_ext = 0;
  _has_prev_ext = false;
  _dw = 0;
  _rule = nullptr;
  _wire = wire;
}

#define DB_WIRE_PATH_ITR_INVALID_OPCODE 0

bool dbWirePathItr::getNextPath(dbWirePath& path)
{
  if ((_opcode == dbWireDecoder::PATH) || (_opcode == dbWireDecoder::JUNCTION)
      || (_opcode == dbWireDecoder::SHORT)
      || (_opcode == dbWireDecoder::VWIRE)) {
    dbTechLayerRule* lyr_rule = nullptr;
    _rule = nullptr;

    if (_opcode == dbWireDecoder::JUNCTION) {
      path.junction_id = _decoder.getJunctionValue();
      path.is_branch = true;
    } else {
      path.is_branch = false;
    }

    if ((_opcode == dbWireDecoder::SHORT)
        || (_opcode == dbWireDecoder::VWIRE)) {
      path.is_short = true;
      path.short_junction = _decoder.getJunctionValue();
    } else {
      path.is_short = false;
      path.short_junction = 0;
    }

    path.iterm = nullptr;
    path.bterm = nullptr;
    path.layer = _decoder.getLayer();

  get_point:

    _opcode = _decoder.next();

    if (_opcode == dbWireDecoder::POINT) {
      _decoder.getPoint(_prev_x, _prev_y);
      _prev_ext = 0;
      _has_prev_ext = false;

      if (path.is_branch == false) {
        path.junction_id = _decoder.getJunctionId();
      }
    } else if (_opcode == dbWireDecoder::POINT_EXT) {
      _decoder.getPoint(_prev_x, _prev_y, _prev_ext);
      _has_prev_ext = true;

      if (path.is_branch == false) {
        path.junction_id = _decoder.getJunctionId();
      }

    } else if (_opcode == dbWireDecoder::RULE) {
      lyr_rule = _decoder.getRule();
      _rule = lyr_rule->getNonDefaultRule();
      goto get_point;  // rules preceded a point...
    } else {
      assert(DB_WIRE_PATH_ITR_INVALID_OPCODE);
    }

    path.point.setX(_prev_x);
    path.point.setY(_prev_y);

    // Check for sequence: (BTERM), (ITERM), (BTERM, ITERM) or, (ITERM BTERM)
    dbWireDecoder::OpCode next_opcode = _decoder.peek();

    if (next_opcode == dbWireDecoder::BTERM) {
      _opcode = _decoder.next();
      path.bterm = _decoder.getBTerm();
    }

    else if (next_opcode == dbWireDecoder::ITERM) {
      _opcode = _decoder.next();
      path.iterm = _decoder.getITerm();
    }

    next_opcode = _decoder.peek();

    if (next_opcode == dbWireDecoder::BTERM) {
      _opcode = _decoder.next();
      path.bterm = _decoder.getBTerm();
    }

    else if (next_opcode == dbWireDecoder::ITERM) {
      _opcode = _decoder.next();
      path.iterm = _decoder.getITerm();
    }

    path.rule = _rule;
    if (lyr_rule) {
      _dw = lyr_rule->getWidth() >> 1;
    } else {
      _dw = _decoder.getLayer()->getWidth() >> 1;
    }
    _opcode = _decoder.next();
    return true;
  }

  return false;
}

inline void dbWirePathItr::getTerms(dbWirePathShape& s)
{
  // Check for sequence: (BTERM), (ITERM), (BTERM, ITERM) or, (ITERM BTERM)
  dbWireDecoder::OpCode next_opcode = _decoder.peek();

  if (next_opcode == dbWireDecoder::BTERM) {
    _opcode = _decoder.next();
    s.bterm = _decoder.getBTerm();
  }

  else if (next_opcode == dbWireDecoder::ITERM) {
    _opcode = _decoder.next();
    s.iterm = _decoder.getITerm();
  }

  next_opcode = _decoder.peek();

  if (next_opcode == dbWireDecoder::BTERM) {
    _opcode = _decoder.next();
    s.bterm = _decoder.getBTerm();
  }

  else if (next_opcode == dbWireDecoder::ITERM) {
    _opcode = _decoder.next();
    s.iterm = _decoder.getITerm();
  }
}

bool dbWirePathItr::getNextShape(dbWirePathShape& s)
{
  s.iterm = nullptr;
  s.bterm = nullptr;
nextOpCode:
  switch (_opcode) {
    case dbWireDecoder::PATH:
    case dbWireDecoder::JUNCTION:
    case dbWireDecoder::SHORT:
    case dbWireDecoder::VWIRE:
      return false;

    case dbWireDecoder::RECT: {
      // order wires doesn't understand RECTs so just hide them for now
      // they aren't required to establish connectivity in our flow
      _opcode = _decoder.next();
      goto nextOpCode;
    }
    case dbWireDecoder::POINT: {
      int cur_x;
      int cur_y;
      _decoder.getPoint(cur_x, cur_y);
      s.junction_id = _decoder.getJunctionId();
      s.point.setX(cur_x);
      s.point.setY(cur_y);
      s.layer = _decoder.getLayer();
      s.shape.setSegment(_prev_x,
                         _prev_y,
                         _prev_ext,
                         _has_prev_ext,
                         cur_x,
                         cur_y,
                         0,
                         false,
                         _dw,
                         _dw,
                         s.layer);
      getTerms(s);

      _prev_x = cur_x;
      _prev_y = cur_y;
      _prev_ext = 0;
      _has_prev_ext = false;
      break;
    }

    case dbWireDecoder::POINT_EXT: {
      int cur_x;
      int cur_y;
      int cur_ext;
      _decoder.getPoint(cur_x, cur_y, cur_ext);

      //
      // By defintion a colinear-point with an extension must begin
      // a new path-segment
      //
      if ((cur_x == _prev_x) && (cur_y == _prev_y)) {
        _prev_ext = cur_ext;
        _has_prev_ext = true;
        _opcode = _decoder.next();
        goto nextOpCode;
      }

      s.junction_id = _decoder.getJunctionId();
      s.point.setX(cur_x);
      s.point.setY(cur_y);
      s.layer = _decoder.getLayer();
      s.shape.setSegment(_prev_x,
                         _prev_y,
                         _prev_ext,
                         _has_prev_ext,
                         cur_x,
                         cur_y,
                         cur_ext,
                         true,
                         _dw,
                         _dw,
                         s.layer);
      getTerms(s);

      _prev_x = cur_x;
      _prev_y = cur_y;
      _prev_ext = cur_ext;
      _has_prev_ext = true;
      break;
    }

    case dbWireDecoder::VIA: {
      dbVia* via = _decoder.getVia();
      dbBox* box = via->getBBox();
      Rect b = box->getBox();
      int xmin = b.xMin() + _prev_x;
      int ymin = b.yMin() + _prev_y;
      int xmax = b.xMax() + _prev_x;
      int ymax = b.yMax() + _prev_y;
      Rect r(xmin, ymin, xmax, ymax);
      s.junction_id = _decoder.getJunctionId();
      s.point.setX(_prev_x);
      s.point.setY(_prev_y);
      s.layer = _decoder.getLayer();
      s.shape.setVia(via, r);
      getTerms(s);
      _dw = _decoder.getLayer()->getWidth() >> 1;
      if (_rule) {
        dbTechLayerRule* lyr_rule = _rule->getLayerRule(_decoder.getLayer());
        if (lyr_rule) {
          _dw = lyr_rule->getWidth() >> 1;
        }
      }
      _prev_ext = 0;
      _prev_ext = 0;
      _has_prev_ext = false;
      break;
    }

    case dbWireDecoder::TECH_VIA: {
      dbTechVia* via = _decoder.getTechVia();
      dbBox* box = via->getBBox();
      Rect b = box->getBox();
      int xmin = b.xMin() + _prev_x;
      int ymin = b.yMin() + _prev_y;
      int xmax = b.xMax() + _prev_x;
      int ymax = b.yMax() + _prev_y;
      Rect r(xmin, ymin, xmax, ymax);
      s.junction_id = _decoder.getJunctionId();
      s.point.setX(_prev_x);
      s.point.setY(_prev_y);
      s.layer = _decoder.getLayer();
      s.shape.setVia(via, r);
      getTerms(s);
      _dw = _decoder.getLayer()->getWidth() >> 1;
      if (_rule) {
        dbTechLayerRule* lyr_rule = _rule->getLayerRule(_decoder.getLayer());
        if (lyr_rule) {
          _dw = lyr_rule->getWidth() >> 1;
        }
      }
      _prev_ext = 0;
      _has_prev_ext = false;
      break;
    }

    case dbWireDecoder::RULE: {
      dbTechLayerRule* rule = _decoder.getRule();
      _dw = rule->getWidth() >> 1;
      _opcode = _decoder.next();
      goto nextOpCode;
    }

    case dbWireDecoder::END_DECODE:
      return false;

    default:
      ZASSERT(DB_WIRE_PATH_ITR_INVALID_OPCODE);
      _opcode = _decoder.next();
      goto nextOpCode;
  }

  _opcode = _decoder.next();
  return true;
}

//
// Routines for struct dbWirePath here
//
void dbWirePath::dump(utl::Logger* logger, const char* group, int level) const
{
  debugPrint(logger,
             utl::ODB,
             group,
             level,
             "Path id: {}  at {} {}  ",
             junction_id,
             point.getX(),
             point.getY());
  if (layer) {
    debugPrint(logger, utl::ODB, group, level, "layer {}  ", layer->getName());
  } else {
    debugPrint(logger, utl::ODB, group, level, "NO LAYER  ");
  }

  if (rule) {
    debugPrint(logger,
               utl::ODB,
               group,
               level,
               "non-default rule {} ",
               rule->getName());
  }

  if (iterm) {
    debugPrint(logger,
               utl::ODB,
               group,
               level,
               "Connects to Iterm {}  ",
               iterm->getMTerm()->getName());
  }

  if (bterm) {
    debugPrint(logger,
               utl::ODB,
               group,
               level,
               "Connects to Bterm {}  ",
               bterm->getName());
  }

  if (is_branch) {
    debugPrint(logger, utl::ODB, group, level, "is branch  ");
  }

  if (is_short) {
    debugPrint(
        logger, utl::ODB, group, level, "is short to {} ", short_junction);
  }
}

//
// Routines for struct dbWirePathShape here
//
void dbWirePathShape::dump(utl::Logger* logger,
                           const char* group,
                           int level) const
{
  debugPrint(logger,
             utl::ODB,
             group,
             level,
             "WireShape id: {}  at {} {}  ",
             junction_id,
             point.getX(),
             point.getY());

  if (layer) {
    debugPrint(logger, utl::ODB, group, level, "layer {}  ", layer->getName());
  } else {
    debugPrint(logger, utl::ODB, group, level, "NO LAYER  ");
  }

  if (iterm) {
    debugPrint(logger,
               utl::ODB,
               group,
               level,
               "Connects to Iterm {} {}  ",
               iterm->getId(),
               iterm->getMTerm()->getName());
  }

  if (bterm) {
    debugPrint(logger,
               utl::ODB,
               group,
               level,
               "Connects to Bterm {} {}  ",
               bterm->getId(),
               bterm->getName());
  }

  shape.dump(logger, group, level);
}

//
//  Inline routines of dbShape are in dbShape.h -- non-inlines are here
//
void dbShape::dump(utl::Logger* logger, const char* group, int level) const
{
  debugPrint(logger,
             utl::ODB,
             group,
             level,
             "Shape at ({} {}) ({} {}) of type ",
             xMin(),
             yMin(),
             xMax(),
             yMax());

  switch (getType()) {
    case VIA: {
      debugPrint(logger, utl::ODB, group, level, "Block Via:  ");
      break;
    }

    case TECH_VIA: {
      debugPrint(logger,
                 utl::ODB,
                 group,
                 level,
                 "Tech Via {}  ",
                 getTechVia()->getName());
      break;
    }

    case SEGMENT: {
      debugPrint(logger,
                 utl::ODB,
                 group,
                 level,
                 "Wire Segment on layer {}  ",
                 getTechLayer()->getName());
      break;
    }
    default:
      break;
  }
}

//
// Utility to dump out wire path iterator for a net.
//
void dumpWirePaths4Net(dbNet* innet, const char* group, int level)
{
  if (!innet) {
    return;
  }
  utl::Logger* logger = innet->getImpl()->getLogger();

  const char* prfx = "dumpWirePaths:";
  dbWire* wire0 = innet->getWire();
  if (!wire0) {
    logger->warn(
        utl::ODB, 87, "{} No wires for net {}", prfx, innet->getName());
    return;
  }
  debugPrint(logger,
             utl::ODB,
             group,
             level,
             "{} Dumping wire paths for net {}",
             prfx,
             innet->getName());
  dbWirePathItr pitr;
  struct dbWirePath curpath;
  struct dbWirePathShape curshp;
  pitr.begin(wire0);

  while (pitr.getNextPath(curpath)) {
    curpath.dump(logger, group, level);
    while (pitr.getNextShape(curshp)) {
      curshp.dump(logger, group, level);
    }
  }
}

}  // namespace odb
