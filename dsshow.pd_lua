local ds = pd.Class:new():register("dsshow")
local shapes = require("dsshow-shapes")

function ds:initialize(sel, atoms)
    self.name = sel
    self.args = atoms
    self.inlets = 1
    self.size = {619, 384}
    self.scale = 1
    self:set_size(self.size[1], self.size[2])
    self.delay_time = 25
    self.time = 0
    self.googly = 0
    self.track_orientation = false
    self.touch_size = 20
    self:reset_colors()
    self.state = {
        button_digital_left = 0,
        button_digital_up = 0,
        button_digital_right = 0,
        button_digital_down = 0,
        button_action_square = 0,
        button_action_cross = 0,
        button_action_circle = 0,
        button_action_triangle = 0,
        button_mute = 0,
        button_options = 0,
        button_create = 0,
        button_ps = 0,
        button_l1 = 0,
        button_r1 = 0,
        button_l2 = 0,
        button_r2 = 0,
        button_l3 = 0,
        button_r3 = 0,
        button_pad = 0,
        pad1_touch = 0,
        pad1_x = 0,
        pad1_y = 0,
        pad2_touch = 0,
        pad2_x = 0,
        pad2_y = 0,
        trigger_l = 0,
        trigger_r = 0,
        analog_l_x = 0,
        analog_l_y = 0,
        analog_r_x = 0,
        analog_r_y = 0,
        quat_w = 1,
        quat_x = 0,
        quat_y = 0,
        quat_z = 0,
        impulse_x = 0,
        impulse_y = 0,
        impulse_z = 0,
    }

    self.cube_center = {289, 63}
    self.cube_size = 24

    self.cube_points = {
        { 1, -1,  1},
        {-1, -1,  1},
        {-1, -1, -1},
        { 1, -1, -1},
        { 1,  1,  1},
        {-1,  1,  1},
        {-1,  1, -1},
        { 1,  1, -1},
    }
    self.cube_points_transformed = { -- FIXME: is this of any use concerning preallocation?
        {0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},
    }

    self.cube_connections = {
        {5, 6}, {6, 7}, {7, 8}, {8, 5}, -- top
        {1, 2}, {2, 3}, {3, 4}, {4, 1}, -- bottom
        {1, 5}, {2, 6}, {3, 7}, {4, 8}, -- connecting bottom w/ top
    }

    return true
end

function ds:table_to_string(t)
    local output = ""
    for _, value in ipairs(t) do
        output = output .. " " .. value
    end
    return output
end

function ds:reset_colors(theme_name)
    local themes = {
        light = {
            cover = {220, 224, 230},
            active = {255, 77, 100},
            button = {255, 255, 255},
            print = {48, 48, 48},
            led = {35, 70, 180},
            inlay = {0, 0, 0},
            case = {35, 37, 38},
        },
        dark = {
            cover = {20, 20, 20},
            active = {255, 0, 0},
            button = {64, 64, 64},
            print = {196, 196, 196},
            led = {180, 10, 30},
            inlay = {0, 0, 0},
            case = {48, 48, 48},
        },
    }
    local theme = themes[theme_name] or themes.light
    self.color_cover = theme.cover
    self.color_active = theme.active
    self.color_button = theme.button
    self.color_print = theme.print
    self.color_led = theme.led
    self.color_inlay = theme.inlay
    self.color_case = theme.case
end

function ds:postinitialize()
    self.clock = pd.Clock:new():register(self, "tick")
    self.clock:delay(self.delay_time)
end

function ds:finalize()
    self.clock:destruct()
end

function ds:blend_color(col1, col2, t)
    t = math.max(math.min(t, 1), 0) -- clip 0..1
    local color = {0, 0, 0}
    for i = 1, 3 do
        color[i] = (1 - t) * col1[i] + t * col2[i]
    end
    return color
end

function ds:state_color(input, fg, bg)
    fg = fg or self.color_active
    bg = bg or self.color_button
    return table.unpack(self:blend_color(bg, fg, self.state[input]))
end

function ds:tick()
    self.state.pad1_touch = self.state.pad1_touch - 2 / self.delay_time
    self.state.pad2_touch = self.state.pad2_touch - 2 / self.delay_time
    self.time = self.time + 1 / self.delay_time

    self:repaint(2)
    self:repaint(4)
    self.clock:delay(self.delay_time)
end

function ds:in_1_reload()
    pd.post("reloading [" .. self.name .. self:table_to_string(self.args) .. "]")
   self:dofilex(self._scriptname)
end

function ds:in_1_color(x)
    if x[1] == "reset" or x[1] == "theme" then
        self:reset_colors(x[2])
    elseif #x == 4 and
        type(x[2]) == "number" and
        type(x[3]) == "number" and
        type(x[4]) == "number" then
        local color = {x[2], x[3], x[4]}
        if x[1] == "case" then self.color_case = color
        elseif x[1] == "active" then self.color_active = color
        elseif x[1] == "cover" then self.color_cover = color
        elseif x[1] == "button" then self.color_button = color
        elseif x[1] == "inlay" then self.color_inlay = color
        elseif x[1] == "led" then self.color_led = color
        elseif x[1] == "print" then self.color_print = color
        else
            self:error(self.name..": unknown element '"..x[1].."' for color setting")
        end
    else 
        self:error(self.name..": invalid color arguments")
    end
    self:repaint()
end

function ds:in_1_googly(atoms)
    self.googly = atoms[1]
end

function ds:in_1_digital(atoms)
    if atoms[1] == "x" then
        self.state.button_digital_left = math.max(atoms[2] * -1)
        self.state.button_digital_right = math.max(atoms[2])
    elseif atoms[1] == "y" then
        self.state.button_digital_down = math.max(atoms[2] * -1)
        self.state.button_digital_up = math.max(atoms[2])
    end
end

function ds:in_1_quat(atoms)
    if not self.track_orientation then self.track_orientation = true end
    self.state.quat_w = atoms[1] or 1
    self.state.quat_x = atoms[2] or 0
    self.state.quat_y = atoms[3] or 0
    self.state.quat_z = atoms[4] or 0
end

function ds:in_1_impulse(atoms)
    self.state.impulse_x = atoms[1] or 0
    self.state.impulse_y = atoms[2] or 0
    self.state.impulse_z = atoms[3] or 0
end

function ds:in_1_button(atoms)
    if atoms[1] == "circle" then self.state.button_action_circle = atoms[2] end
    if atoms[1] == "square" then self.state.button_action_square = atoms[2] end
    if atoms[1] == "triangle" then self.state.button_action_triangle = atoms[2] end
    if atoms[1] == "cross" then self.state.button_action_cross = atoms[2] end

    if atoms[1] == "mute" then self.state.button_mute = atoms[2] end
    if atoms[1] == "options" then self.state.button_options = atoms[2] end
    if atoms[1] == "create" then self.state.button_create = atoms[2] end
    if atoms[1] == "ps" then self.state.button_ps = atoms[2] end

    if atoms[1] == "l1" then self.state.button_l1 = atoms[2] end
    if atoms[1] == "r1" then self.state.button_r1 = atoms[2] end
    if atoms[1] == "l2" then self.state.button_l2 = atoms[2] end
    if atoms[1] == "r2" then self.state.button_r2 = atoms[2] end

    if atoms[1] == "l3" then self.state.button_l3 = atoms[2] end
    if atoms[1] == "r3" then self.state.button_r3 = atoms[2] end

    if atoms[1] == "pad" then self.state.button_pad = atoms[2] end
end

function ds:in_1_trigger(atoms)
    if atoms[1] == "l" then
        self.state.trigger_l = atoms[2] * 3
    elseif atoms[1] == "r" then
        self.state.trigger_r = atoms[2] * 3
    end
end

function ds:in_1_analog(atoms)
    if atoms[1] == "l" then
        if atoms[2] == "x" then
            self.state.analog_l_x = atoms[3] * 30
        elseif atoms[2] == "y" then
            self.state.analog_l_y = atoms[3] * 30
        end
    elseif atoms[1] == "r" then
        if atoms[2] == "x" then
            self.state.analog_r_x = atoms[3] * 30
        elseif atoms[2] == "y" then
            self.state.analog_r_y = atoms[3] * 30
        end
    end
end

function ds:in_1_pad(atoms)
    if atoms[1] == "touch1" then
        if atoms[2] == "x" then
            local touched = atoms[3] >= 0
            self.state.pad1_touch = touched and 1.2 or 0
            if touched then self.state.pad1_x = atoms[3] end
        elseif atoms[2] == "y" then
            local touched = atoms[3] >= 0
            self.state.pad1_touch = touched and 1.2 or 0
            if touched then self.state.pad1_y = atoms[3] end
        end
    elseif atoms[1] == "touch2" then
        if atoms[2] == "x" then
            local touched = atoms[3] >= 0
            self.state.pad2_touch = touched and 1.2 or 0
            if touched then self.state.pad2_x = atoms[3] end
        elseif atoms[2] == "y" then
            local touched = atoms[3] >= 0
            self.state.pad2_touch = touched and 1.2 or 0
            if touched then self.state.pad2_y = atoms[3] end
        end
    end
end

function ds:in_1_scale(x)
    self.scale = x[1]
    self:set_size(self.size[1]*self.scale, self.size[2]*self.scale)
end

function ds:in_1_size(x)
    self.scale = x[1] == "reset" and 1 or x[1] / self.size[1]
    self:set_size(self.size[1]*self.scale, self.size[2]*self.scale)
end

function ds:in_1(atoms)
    -- generic method to safely ignore unknown messages
end

function ds:transform_point(point)
    local outpoint = {0, 0, 0}
    local state = self.state
    outpoint[1] = point[1] - state.impulse_x * 2
    outpoint[2] = point[2] - state.impulse_y * 2
    outpoint[3] = point[3] - state.impulse_z * 2
    outpoint = shapes.rotateVectorByQuaternion(outpoint, {
        state.quat_w,
        state.quat_x,
        -state.quat_y,
        -state.quat_z
    })
    return outpoint
end

function ds:paint(g) -- paint background
    g:scale(self.scale, self.scale)
    g:translate(22, 6)

    g:set_color(table.unpack(self.color_case)); g:fill_path(shapes.paths.background)
    -- g:reset_transform()
end

function ds:paint_layer_2(g) -- paint background buttons and led
    local state = self.state
    local color_led = self:blend_color({0, 0, 0}, self.color_led, math.sin(self.time) * 0.2 + 0.8)

    g:scale(self.scale, self.scale)
    g:translate(22, 6)

    g:set_color(table.unpack(color_led)); g:fill_path(shapes.paths.led)

    g:translate(0, state.button_l1*3)
    g:set_color(self:state_color("button_l1", self.color_active, self.color_case)); g:fill_path(shapes.paths.button_l1)
    g:translate(0, state.button_r1*3-state.button_l1*3)
    g:set_color(self:state_color("button_r1", self.color_active, self.color_case)); g:fill_path(shapes.paths.button_r1)
    g:translate(0, -state.button_r1*3)

    g:translate(48, 28)
    g:set_color(self:state_color("button_l2", self.color_active, self.color_case)); g:fill_path(shapes.path_button_l2(-state.trigger_l*0.28-0.3))
    g:translate(480, 0)
    g:set_color(self:state_color("button_r2", self.color_active, self.color_case)); g:fill_path(shapes.path_button_r2(state.trigger_r*0.28+0.3))
    g:translate(-528, -28)
end

function ds:paint_layer_3(g) -- paint cover and inlays
    g:scale(self.scale, self.scale)
    g:translate(22, 6)

    g:set_color(table.unpack(self.color_cover)); for i = 1, #shapes.path_groups.cover do g:fill_path(shapes.path_groups.cover[i]) end
    g:set_color(table.unpack(self.color_inlay)); for i = 1, #shapes.path_groups.inlays do g:fill_path(shapes.path_groups.inlays[i]) end
    g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.paths.inlay_pd)
end

function ds:paint_layer_4(g) -- pad and buttons
    local state = self.state
    local color_led = self:blend_color({0, 0, 0}, self.color_led, math.sin(self.time) * 0.2 + 0.8)

    g:scale(self.scale, self.scale)
    g:translate(22, 6)

    g:set_color(self:state_color("button_pad", self.color_active, self.color_cover)); g:fill_path(shapes.paths.pad)
    g:set_color(self:state_color("button_mute")); g:fill_path(shapes.paths.button_mute)
    g:set_color(self:state_color("button_options")); g:fill_path(shapes.paths.button_options)
    g:set_color(self:state_color("button_create")); g:fill_path(shapes.paths.button_create)
    -- g:set_color(self:state_color("button_ps", self.color_active, self.color_case)); for _, shape in ipairs(shapes.button_ps()) do g:fill_path(shape) end

    g:set_color(self:state_color("button_ps", self.color_active, self.color_case)); for i = 1, #shapes.path_groups.button_pd do g:fill_path(shapes.path_groups.button_pd[i]) end
    g:set_color(table.unpack(self.color_inlay)); for i = 1, #shapes.path_groups.button_pd_content do g:fill_path(shapes.path_groups.button_pd_content[i]) end

    g:set_color(self:state_color("button_digital_left")); g:fill_path(shapes.paths.button_digital_left)
    g:set_color(table.unpack(self.color_print)); g:fill_path(shapes.paths.button_digital_left_content)

    g:set_color(self:state_color("button_digital_up")); g:fill_path(shapes.paths.button_digital_up)
    g:set_color(table.unpack(self.color_print)); g:fill_path(shapes.paths.button_digital_up_content)

    g:set_color(self:state_color("button_digital_right")); g:fill_path(shapes.paths.button_digital_right)
    g:set_color(table.unpack(self.color_print)); g:fill_path(shapes.paths.button_digital_right_content)

    g:set_color(self:state_color("button_digital_down")); g:fill_path(shapes.paths.button_digital_down)
    g:set_color(table.unpack(self.color_print)); g:fill_path(shapes.paths.button_digital_down_content)

    g:set_color(self:state_color("button_action_square")); g:fill_path(shapes.paths.button_action_square)
    g:set_color(table.unpack(self.color_print)); g:stroke_rect(415.7, 96.7, 20, 20, 1)

    g:set_color(self:state_color("button_action_cross")); g:fill_path(shapes.paths.button_action_cross)
    g:set_color(table.unpack(self.color_print)); g:draw_line(458, 139, 476, 157, 1); g:draw_line(458, 157, 476, 139, 1)

    g:set_color(self:state_color("button_action_triangle")); g:fill_path(shapes.paths.button_action_triangle)
    g:set_color(table.unpack(self.color_print)); g:stroke_path(shapes.paths.button_action_triangle_content, 1)

    g:set_color(self:state_color("button_action_circle")); g:fill_path(shapes.paths.button_action_circle)
    g:set_color(table.unpack(self.color_print)); g:stroke_ellipse(497, 94, 23.8, 23.8, 1)

    g:translate(state.analog_l_x, -state.analog_l_y)
    g:set_color(self:state_color("button_l3")); g:fill_path(shapes.paths.analog_l)
        g:translate((state.analog_l_x + 5) * self.googly, -state.analog_l_y * self.googly) -- FIXME: remove?
        g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.paths.analog_l_content)
        g:translate(-(state.analog_l_x + 5) * self.googly, state.analog_l_y * self.googly)
    g:translate(state.analog_r_x - state.analog_l_x, state.analog_l_y - state.analog_r_y)
    g:set_color(self:state_color("button_r3")); g:fill_path(shapes.paths.analog_r)
        g:translate((state.analog_r_x - 5) * self.googly, -state.analog_r_y * self.googly)
        g:set_color(table.unpack(self.color_inlay)); g:fill_path(shapes.paths.analog_r_content)
        g:translate(-(state.analog_r_x - 5) * self.googly, state.analog_r_y * self.googly)
    g:translate(-state.analog_r_x, state.analog_r_y)

    if self.track_orientation then
        local points = self.cube_points
        local points_transformed = self.cube_points_transformed
        local connections = self.cube_connections
        g:set_color(table.unpack(color_led))
        for i = 1, #points do
            points_transformed[i] = self:transform_point(points[i])
        end
        for i = 1, #connections do
            local from = points_transformed[connections[i][1]]
            local to = points_transformed[connections[i][2]]
            local scale_from = self.cube_size * 5 / (5 - from[2])
            local scale_to = self.cube_size * 5 / (5 - to[2])
            local strokewidth = 1
            if i <= 4 then strokewidth = 3 end
            g:draw_line(
                -from[1] * scale_from + self.cube_center[1],
                -from[3] * scale_from + self.cube_center[2],
                -to[1] * scale_to + self.cube_center[1],
                -to[3] * scale_to + self.cube_center[2], strokewidth)
        end
    end

    if state.pad1_touch > 0 then
        g:set_color(self:state_color("pad1_touch", self.color_active, self.color_cover))
        g:fill_ellipse(state.pad1_x * 180 + 190, state.pad1_y * 96 + 10, self.touch_size, self.touch_size)
        g:set_color(table.unpack(self.color_cover))
        g:draw_text("1", state.pad1_x * 180 + 197, state.pad1_y * 96 + 14, 10, 12)
    end

    if state.pad2_touch > 0 then
        g:set_color(self:state_color("pad2_touch", self.color_active, self.color_cover))
        g:fill_ellipse(state.pad2_x * 180 + 190, state.pad2_y * 96 + 10, self.touch_size, self.touch_size)
        g:set_color(table.unpack(self.color_cover))
        g:draw_text("2", state.pad2_x * 180 + 197, state.pad2_y * 96 + 14, 10, 12)
    end
end
