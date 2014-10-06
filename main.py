import datetime
import jinja2
import json
import logging
import os
import webapp2
from google.appengine.ext import ndb
from google.appengine.ext.webapp import template

JINJA_ENV = jinja2.Environment(
  loader=jinja2.FileSystemLoader(os.path.dirname(__file__)),
  extensions=['jinja2.ext.autoescape'],
  autoescape=True,
)

class ThermostatData(ndb.Model):
  temperature = ndb.IntegerProperty('t')
  humidity = ndb.IntegerProperty('h')
  num_averaged = ndb.IntegerProperty('n', default=1)
  set_temperature = ndb.IntegerProperty('s')
  hold = ndb.BooleanProperty('o')
  time = ndb.DateTimeProperty('i', indexed=True)
  heat_on = ndb.BooleanProperty('e')

  @classmethod
  def get_key(cls, t_id):
    return ndb.Key('Thermostat', t_id)

  @classmethod
  def query_readings(cls, t_id):
    key = cls.get_key(t_id)
    return cls.query(ancestor=key).order(-cls.time)

  @classmethod
  def query_oneday_readings(cls, t_id):
    key = cls.get_key(t_id)
    one_day_ago = datetime.datetime.utcnow() - datetime.timedelta(hours=24)
    return cls.query(cls.time > one_day_ago, ancestor=key).order(-cls.time)


class PostData(webapp2.RequestHandler):
  def get(self):
    time_now = datetime.datetime.utcnow()
    # Get some of the values from the query string
    t_id = self.request.get('id')
    if not t_id:
      self.response.write('Error: invalid ID')
      return

    temp = self.request.get('temp')
    hum = self.request.get('hum')
    if not temp or not hum:
      self.response.write('Error: invalid parameters')
      return

    # Convert temp and humidity for storage as integers
    temp = int(temp)
    hum = int(hum)

    # Get previously saved record
    prev_reading = last_reading = None
    readings = ThermostatData.query_readings(t_id).fetch(2)
    if len(readings) == 0:
      # Create a fake last_reading with some default values
      last_reading = ThermostatData(
          temperature=temp, humidity=hum,
          set_temperature=680, hold=False, heat_on=False
      )
    else:
      last_reading = readings[0]
      if len(readings) > 1:
        prev_reading = readings[1]

    # Determine whether hold temperature is in request
    hold = self.request.get('hold', None)
    if hold is None:
      hold = last_reading.hold
    else:
      hold = (hold == 'y')

    # Determine whether set temperature is in request
    set_temp = self.request.get('set', None)
    if set_temp is None:
      if hold:
        # Holding temp, ignore schedule
        set_temp = last_reading.set_temperature
      else:
        scheduled_temp = get_scheduled_temp()
        if scheduled_temp is None:
          set_temp = last_reading.set_temperature
        else:
          set_temp = scheduled_temp
    else:
      set_temp = int(set_temp)

    # Determine whether to turn heat on or off
    if last_reading.heat_on:
      heat_on = (temp < (set_temp + 10))
    else:
      heat_on = (temp < (set_temp - 5))

    logging.info('%s,%s,%s,%s,%s' % (temp,hum,set_temp,hold,heat_on))

    storage_interval = datetime.timedelta(minutes=5, seconds=10)
    if not prev_reading or prev_reading.time + storage_interval < time_now:
      new_data = ThermostatData(
          parent=ThermostatData.get_key(t_id),
          time=time_now,
          temperature=temp,
          humidity=hum,
          set_temperature=set_temp,
          hold=hold,
          heat_on=heat_on,
      )
      new_data.put()
    else:
      # Average together last 5 minutes worth of readings to reduce data storage
      num_averaged = last_reading.num_averaged
      last_reading.populate(
        time=time_now,
        temperature=add_value_to_average(last_reading.temperature, temp, num_averaged),
        humidity=add_value_to_average(last_reading.humidity, hum, num_averaged),
        num_averaged=num_averaged + 1,
        set_temperature=set_temp,
        hold=hold,
        heat_on=heat_on,
      )
      last_reading.put()


    self.response.write('%s,%s,%s' % (set_temp, int(hold), int(heat_on)))


class GetData(webapp2.RequestHandler):
  def get(self):
    t_id = self.request.get('id')
    reading = ThermostatData.query_readings(t_id).get()
    self.response.write('%s' % int(reading.heat_on))


class Thermostat(webapp2.RequestHandler):
  def get(self):
    t_id = self.request.get('id')
    if not t_id:
      self.response.write('Error: invalid ID')
      return

    # Put readings into template data
    readings = ThermostatData.query_oneday_readings(t_id)
    last_reading = None
    values = []
    for reading in readings:
      if last_reading is None:
        last_reading = reading
      time_str = str(reading.time)
      values.append("['%s',%s,%s,%s]" %
          (time_str.split('.')[0], reading.temperature, reading.humidity, reading.set_temperature))
    status = {}
    if last_reading:
      status = {
        'heat': last_reading.heat_on,
        'hold': last_reading.hold,
      }
    template_values = {
      'data': '[' + ','.join(values) + ']',
      'status': json.dumps(status),
    }
    template = JINJA_ENV.get_template('index.html')
    self.response.write(template.render(template_values))

def add_value_to_average(old_value, new_value, num_averaged):
  return (old_value * num_averaged + new_value) / (num_averaged + 1)

def get_scheduled_temp():
  return None

app = webapp2.WSGIApplication([
    ('/postdata', PostData),
    ('/getheat', GetData),
    ('/', Thermostat),
], debug=True)

# TODO: Cron job to clean out or consolidate older data
# TODO: Form for turning temp up or down
# TODO: Read schedule into Datastore
# TODO: Modify temperature according to schedule
# TODO: Don't draw lines when gap > 15 minutes
# TODO: Add authentication to change temp, view details
# TODO: Add secret random code to put into Arduino modules for security
